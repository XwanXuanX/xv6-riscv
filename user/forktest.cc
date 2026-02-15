// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "user/user.h"

#define N 1000

void print(const char *s) { write(1, s, strlen(s)); }

void forktest() {
    int n;

    print("fork test\n");

    for (n = 0; n < N; n++) {
        const int pid = fork();
        if (pid < 0) {
            break;
        }
        if (pid == 0) {
            exit(0);
        }
    }

    if (n == N) {
        print("fork claimed to work N times!\n");
        exit(1);
    }

    for (; n > 0; n--) {
        if (wait(nullptr) < 0) {
            print("wait stopped early\n");
            exit(1);
        }
    }

    if (wait(nullptr) != -1) {
        print("wait got too many\n");
        exit(1);
    }

    print("fork test OK\n");
}

int main() {
    forktest();
    exit(0);
}
