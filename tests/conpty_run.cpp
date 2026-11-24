// conpty_run: runs one program in a Windows pseudo console (ConPTY), the
// Windows stand-in for script(1) in tests/run-integrator-menu.sh.
//
//   conpty_run <program> [args...]
//
// stdin goes to the console as typed input. When stdin ends, Ctrl+Z and Enter
// are typed once, the counterpart of the ^D that script(1) sends. The console
// output is stripped of VT control sequences and copied to stdout. The helper
// exits with the exit code of the program.
//
// CONPTY_RUN_RAW=<file>: append the unfiltered console output to <file>.

// CreatePseudoConsole is declared for Windows 10 1809 (NTDDI_WIN10_RS5) and later.
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000006
#define NOMINMAX 1  // no min/max macros from windows.h: they break std::max
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// Removes VT control sequences from the console output. ConPTY writes runs of
// spaces as cursor-forward moves and may move down with cursor positioning, so
// those become spaces and line breaks; all other sequences are dropped.
class VtFilter {
public:
    std::string feed(const char *data, size_t size) {
        std::string out;
        for (size_t i = 0; i < size; ++i)
            step(data[i], out);
        return out;
    }

private:
    enum State { kText, kEscape, kCsi, kOsc, kOscEscape };
    State state_ = kText;
    std::string params_;  // parameter and intermediate bytes of a CSI sequence
    long line_ = 1;       // current line: 1 + line breaks passed on so far

    void step(char c, std::string &out) {
        switch (state_) {
        case kText:
            if (c == '\x1b') {
                state_ = kEscape;
                return;
            }
            if (c == '\n')
                ++line_;
            out += c;
            return;
        case kEscape:
            escape(c);
            return;
        case kCsi:  // ESC [ <params> <final byte 0x40-0x7E>
            if (c >= 0x40 && c <= 0x7e) {
                csi(c, out);
                state_ = kText;
            } else {
                params_ += c;
            }
            return;
        case kOsc:  // ESC ] ... ends with BEL or ESC backslash
            if (c == '\a')
                state_ = kText;
            else if (c == '\x1b')
                state_ = kOscEscape;
            return;
        case kOscEscape:
            if (c == '\\')
                state_ = kText;
            else
                escape(c);  // another sequence starts and ends the OSC
            return;
        }
    }

    // The byte after ESC: CSI or OSC start, else a two-byte sequence to drop.
    void escape(char c) {
        if (c == '[') {
            params_.clear();
            state_ = kCsi;
        } else if (c == ']') {
            state_ = kOsc;
        } else {
            state_ = kText;
        }
    }

    void csi(char final, std::string &out) {
        const long n = std::strtol(params_.c_str(), nullptr, 10);  // 0 if absent
        if (final == 'C') {  // cursor forward
            out.append(static_cast<size_t>(std::max(1L, n)), ' ');
        } else if (final == 'H' || final == 'f') {  // cursor position, row first
            const long row = std::max(1L, n);
            if (row > line_) {
                out.append(static_cast<size_t>(row - line_), '\n');
                line_ = row;
            }
        }
    }
};

HANDLE g_consoleIn = nullptr;   // our end of the console input pipe
HANDLE g_consoleOut = nullptr;  // our end of the console output pipe
FILE *g_raw = nullptr;          // CONPTY_RUN_RAW file, if set
std::atomic<ULONGLONG> g_lastOutput(0);  // GetTickCount64() of the last output

bool writeAll(HANDLE h, const char *data, size_t size) {
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr))
            return false;
        data += written;
        size -= written;
    }
    return true;
}

// Passes stdin on to the console as soon as it arrives. At the end of stdin,
// types Ctrl+Z and Enter once; the pipe stays open.
DWORD WINAPI forwardInput(LPVOID) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    char buf[256];
    DWORD n = 0;
    while (ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0)
        if (!writeAll(g_consoleIn, buf, n))
            return 0;
    writeAll(g_consoleIn, "\x1a\r", 2);
    return 0;
}

// Reads the console output until the pseudo console is closed
// (ERROR_BROKEN_PIPE) and copies it, filtered, to stdout at once.
DWORD WINAPI forwardOutput(LPVOID) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    VtFilter filter;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(g_consoleOut, buf, sizeof buf, &n, nullptr)) {
        if (n == 0)
            continue;
        g_lastOutput = GetTickCount64();
        if (g_raw) {
            std::fwrite(buf, 1, n, g_raw);
            std::fflush(g_raw);
        }
        const std::string text = filter.feed(buf, n);
        writeAll(out, text.data(), text.size());  // keep draining even if stdout is gone
    }
    return 0;
}

// Joins the arguments into a command line. An empty argument or one with a
// space or tab goes in double quotes; backslashes before the closing quote are
// doubled so they do not escape it.
std::string quoteArg(const std::string &arg) {
    if (!arg.empty() && arg.find_first_of(" \t") == std::string::npos)
        return arg;
    size_t trailing = 0;
    while (trailing < arg.size() && arg[arg.size() - 1 - trailing] == '\\')
        ++trailing;
    return '"' + arg + std::string(trailing, '\\') + '"';
}

// Full path of a program given without a directory, looked up in PATH only:
// CreateProcess would search the system directories first, where bash.exe can
// be the WSL launcher. Empty if a directory is given or nothing is found.
std::string findInPath(const std::string &name) {
    if (name.find_first_of("/\\:") != std::string::npos)
        return std::string();
    const DWORD size = GetEnvironmentVariableA("PATH", nullptr, 0);
    if (size == 0)
        return std::string();
    std::vector<char> path(size);
    if (GetEnvironmentVariableA("PATH", path.data(), size) == 0)
        return std::string();
    char found[MAX_PATH];
    const DWORD n = SearchPathA(path.data(), name.c_str(), ".exe", MAX_PATH, found, nullptr);
    return (n > 0 && n < MAX_PATH) ? std::string(found, n) : std::string();
}

int fail(const char *what) {
    std::fprintf(stderr, "conpty_run: %s failed (error %lu)\n", what, GetLastError());
    return 1;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: conpty_run <program> [args...]\n");
        return 2;
    }
    std::string cmdLine;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.find('"') != std::string::npos) {
            std::fprintf(stderr, "conpty_run: argument %d contains '\"', which is not supported\n", i);
            return 2;
        }
        if (i > 1)
            cmdLine += ' ';
        cmdLine += quoteArg(arg);
    }
    const std::string program = findInPath(argv[1]);

    if (const char *rawPath = std::getenv("CONPTY_RUN_RAW")) {
        g_raw = std::fopen(rawPath, "ab");
        if (!g_raw)
            std::fprintf(stderr, "conpty_run: cannot open CONPTY_RUN_RAW file '%s'\n", rawPath);
    }

    HANDLE inRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &g_consoleIn, nullptr, 0) ||
        !CreatePipe(&g_consoleOut, &outWrite, nullptr, 0))
        return fail("CreatePipe");
    HPCON console = nullptr;
    const COORD size = {200, 50};
    const HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &console);
    // The console keeps its own copies; closing ours lets the output read end
    // with ERROR_BROKEN_PIPE once the console is gone.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr)) {
        std::fprintf(stderr, "conpty_run: CreatePseudoConsole failed (HRESULT 0x%08lx)\n",
                     static_cast<unsigned long>(hr));
        return 1;
    }
    HANDLE outputThread = CreateThread(nullptr, 0, forwardOutput, nullptr, 0, nullptr);
    if (!outputThread)
        return fail("CreateThread");

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuf(attrSize);
    STARTUPINFOEXA si;
    ZeroMemory(&si, sizeof si);
    si.StartupInfo.cb = sizeof si;
    // NULL standard handles: otherwise the child may get the helper's own
    // stdin/stdout (pipes) instead of the pseudo console.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   console, sizeof console, nullptr, nullptr))
        return fail("setting the pseudo console attribute");

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(program.empty() ? nullptr : program.c_str(), &cmdLine[0], nullptr, nullptr,
                        FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi)) {
        const int rc = fail(("starting '" + cmdLine + "'").c_str());
        ClosePseudoConsole(console);
        return rc;
    }
    CloseHandle(pi.hThread);
    HANDLE inputThread = CreateThread(nullptr, 0, forwardInput, nullptr, 0, nullptr);
    if (!inputThread)
        return fail("CreateThread");

    // 1. Wait for the program.
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);

    // 2. Let the console render the last output: wait until 300 ms have passed
    //    without new output (counted from the exit at the earliest), at most 3 s.
    const ULONGLONG exited = GetTickCount64();
    for (;;) {
        const ULONGLONG quietSince = std::max(exited, g_lastOutput.load());
        const ULONGLONG now = GetTickCount64();
        if (now - quietSince >= 300 || now - exited >= 3000)
            break;
        Sleep(10);
    }

    // 3. Close the console; 4. the output thread reads until ERROR_BROKEN_PIPE.
    ClosePseudoConsole(console);
    WaitForSingleObject(outputThread, INFINITE);
    CloseHandle(outputThread);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    if (g_raw)
        std::fclose(g_raw);

    // 5. The input thread may still wait for stdin; returning ends the process.
    return static_cast<int>(exitCode);
}
