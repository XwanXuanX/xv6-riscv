// init: The initial user-level program

#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include <array>

constexpr std::array<const char *, 2> argv = {"sh", nullptr};

int main() {
    if (open("console", O_RDWR) < 0) {
        mknod("console", CONSOLE, 0);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    for (;;) {
        printf("init: starting sh\n");
        const int pid = fork();
        if (pid < 0) {
            printf("init: fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            exec("sh", const_cast<const char **>(argv.data()));
            printf("init: exec sh failed\n");
            exit(1);
        }

        for (;;) {
            // this call to wait() returns if the shell exits,
            // or if a parentless process exits.
            const int wpid = wait(nullptr);
            if (wpid == pid) {
                // the shell exited; restart it.
                break;
            }
            if (wpid < 0) {
                printf("init: wait returned an error\n");
                exit(1);
            } // it was a parentless process; do nothing.
        }
    }
}
