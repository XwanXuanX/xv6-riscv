// Create a zombie process that
// must be reparented at exit.

#include "user/user.h"

int main() {
    if (fork() > 0)
        pause(5); // Let child exit before parent.
    exit(0);
}
