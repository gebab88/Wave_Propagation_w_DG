#include "VideoPipe.hpp"

#include <stdexcept>

#ifdef _WIN32
# include <cstdio>
#else
# include <csignal>
# include <sys/wait.h>
#endif

std::string shellQuote(const std::string &arg)
{
#ifdef _WIN32
    if (arg.find('"') != std::string::npos)
        throw std::invalid_argument("cannot quote an argument containing '\"'");
    return "\"" + arg + "\"";
#else
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
#endif
}

FILE *openWritePipe(const std::string &command)
{
#ifdef _WIN32
    return _popen(("\"" + command + "\"").c_str(), "w");
#else
    return popen(command.c_str(), "w");
#endif
}

PipeStatus closeWritePipe(FILE *pipe)
{
#ifdef _WIN32
    const int status = _pclose(pipe);
    if (status == -1) return {PipeStatus::Unknown, status};
    return {PipeStatus::Exited, status};
#else
    const int status = pclose(pipe);
    if (status != -1 && WIFEXITED(status))
        return {PipeStatus::Exited, WEXITSTATUS(status)};
    if (status != -1 && WIFSIGNALED(status))
        return {PipeStatus::Signaled, WTERMSIG(status)};
    return {PipeStatus::Unknown, status};
#endif
}

#ifdef _WIN32
SigpipeIgnored::SigpipeIgnored() {}
SigpipeIgnored::~SigpipeIgnored() {}
#else
SigpipeIgnored::SigpipeIgnored() : oldHandler_(std::signal(SIGPIPE, SIG_IGN)) {}
SigpipeIgnored::~SigpipeIgnored() { std::signal(SIGPIPE, oldHandler_); }
#endif
