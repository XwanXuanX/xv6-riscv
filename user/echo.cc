#include "user/user.h"
#include <span>

int main(const int argc, std::span<char *> argv) {
    for (int i = 1; i < argc; i++) {
        write(1, argv[i], strlen(argv[i]));
        if (i + 1 < argc) {
            write(1, " ", 1);
        } else {
            write(1, "\n", 1);
        }
    }
    exit(0);
}
