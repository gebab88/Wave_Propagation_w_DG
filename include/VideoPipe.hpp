#ifndef VIDEOPIPE_HPP
#define VIDEOPIPE_HPP

#include <cstdio>
#include <string>

// Portable write pipe to a child process (used to stream frames into ffmpeg).

// Shell quoting for one argument: POSIX sh single quotes ('\'' inside);
// Windows cmd.exe double quotes (throws std::invalid_argument on '"').
std::string shellQuote(const std::string &arg);

// popen(command, "w") on POSIX. On Windows _popen("\"" + command + "\"", "wb"):
// cmd.exe /c strips the first and last quote of a command that starts with a
// quote, so the extra outer pair keeps quoted program paths intact.
FILE *openWritePipe(const std::string &command);

struct PipeStatus {
    enum Kind { Exited, Signaled, Unknown } kind;
    int value;
};

// Closes the pipe and decodes the status: POSIX via WIFEXITED/WTERMSIG,
// Windows: _pclose() is the exit code (-1 -> Unknown).
PipeStatus closeWritePipe(FILE *pipe);

// Ignores SIGPIPE for its lifetime and restores the old handler; no-op on Windows.
class SigpipeIgnored {
public:
    SigpipeIgnored();
    ~SigpipeIgnored();
    SigpipeIgnored(const SigpipeIgnored &) = delete;
    SigpipeIgnored &operator=(const SigpipeIgnored &) = delete;

private:
#ifndef _WIN32
    void (*oldHandler_)(int);
#endif
};

#endif
