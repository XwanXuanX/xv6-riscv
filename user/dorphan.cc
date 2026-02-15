#include "user/user.h"

// Create an orphaned directory and check if test-xv6.py recovers it.

#define BUFSZ 500

char buf[BUFSZ];

int main([[maybe_unused]] int argc, char **argv) {
    char *s = argv[0];

    if (mkdir("dd") != 0) {
        printf("%s: mkdir dd failed\n", s);
        exit(1);
    }

    if (chdir("dd") != 0) {
        printf("%s: chdir dd failed\n", s);
        exit(1);
    }

    if (unlink("../dd") < 0) {
        printf("%s: unlink failed\n", s);
        exit(1);
    }
    printf("wait for kill and reclaim\n");
    // sit around until killed
    for (;;) {
        pause(1000);
    }
}
