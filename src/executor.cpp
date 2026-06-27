#include "executor.h"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
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
