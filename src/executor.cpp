#include "executor.h"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>

int exec_yay(const std::vector<const char*>& argv) {
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        execvp("yay", const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

std::string exec_capture(const std::vector<const char*>& argv) {
    std::string cmd;
    for (size_t i = 0; argv[i] != nullptr; i++) {
        if (i > 0) cmd += ' ';
        cmd += argv[i];
    }
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");
    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    return result;
}
