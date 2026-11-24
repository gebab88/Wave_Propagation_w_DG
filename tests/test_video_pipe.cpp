// Plain C++ checks for VideoPipe (no test framework in this project).
// The binary doubles as its own pipe child: `--copy-stdin <file>` copies stdin
// byte by byte into <file>.
#include "VideoPipe.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
# include <fcntl.h>
# include <io.h>
# include <windows.h>
#endif

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void report(const char *name, int before) {
    if (failures == before) std::printf("PASS %s\n", name);
}

static std::string selfPath;

static int copyStdin(const char *outFile) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    FILE *out = std::fopen(outFile, "wb");
    if (!out) return 2;
    char buf[512];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0)
        std::fwrite(buf, 1, n, out);
    std::fclose(out);
    return 0;
}

static std::vector<unsigned char> readFile(const std::string &path) {
    std::vector<unsigned char> data;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    unsigned char buf[512];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

// Bytes 0..255 four times: contains 0x0A, which text mode would expand.
static std::vector<unsigned char> payload() {
    std::vector<unsigned char> p;
    for (int rep = 0; rep < 4; ++rep)
        for (int b = 0; b < 256; ++b) p.push_back(static_cast<unsigned char>(b));
    return p;
}

// Sends payload() through the pipe into a copy of this binary writing `out`.
static void roundTrip(const std::string &out) {
    std::remove(out.c_str());
    FILE *pipe = openWritePipe(shellQuote(selfPath) + " --copy-stdin " + shellQuote(out));
    CHECK(pipe != nullptr);
    if (!pipe) return;
    const std::vector<unsigned char> sent = payload();
    CHECK(std::fwrite(sent.data(), 1, sent.size(), pipe) == sent.size());
    const PipeStatus st = closeWritePipe(pipe);
    CHECK(st.kind == PipeStatus::Exited);
    CHECK(st.value == 0);
    const std::vector<unsigned char> got = readFile(out);
    if (got.size() != sent.size())
        std::printf("received %zu bytes, sent %zu\n", got.size(), sent.size());
    CHECK(got == sent);
    std::remove(out.c_str());
}

static void test_all_bytes_arrive_unchanged() {
    const int before = failures;
    roundTrip("bin/Release/test_video_pipe.bin");
    report("test_all_bytes_arrive_unchanged", before);
}

static void test_quoted_path_with_space() {
    const int before = failures;
    roundTrip("bin/Release/test video pipe.bin");
    report("test_quoted_path_with_space", before);
}

static void test_exit_code_reported() {
    const int before = failures;
    FILE *pipe = openWritePipe("exit 3");
    CHECK(pipe != nullptr);
    if (pipe) {
        const PipeStatus st = closeWritePipe(pipe);
        CHECK(st.kind == PipeStatus::Exited);
        CHECK(st.value == 3);
    }
    report("test_exit_code_reported", before);
}

static void test_missing_command_reported() {
#ifdef _WIN32
    const char *quiet = " 2>NUL";
#else
    const char *quiet = " 2>/dev/null";
#endif
    const int before = failures;
    SigpipeIgnored guard;
    FILE *pipe = openWritePipe(std::string("dg_no_such_command_xyz") + quiet);
    CHECK(pipe != nullptr);
    if (pipe) {
        const std::vector<unsigned char> block(64 * 1024, 0x55);
        std::fwrite(block.data(), 1, block.size(), pipe);  // may fail: reader is gone
        const PipeStatus st = closeWritePipe(pipe);
        CHECK(st.kind == PipeStatus::Exited);
        CHECK(st.value != 0);
    }
    report("test_missing_command_reported", before);
}

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--copy-stdin")
        return copyStdin(argv[2]);

#ifdef _WIN32
    char path[MAX_PATH];
    const DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    selfPath = std::string(path, len);
#else
    selfPath = argv[0];
#endif

    test_all_bytes_arrive_unchanged();
    test_quoted_path_with_space();
    test_exit_code_reported();
    test_missing_command_reported();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
