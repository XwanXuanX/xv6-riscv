#include "user/user.h"
#include <span>

int main(const int argc, const std::span<char *> argv) {
    if (argc < 2) {
        fprintf(2, "Usage: rm files...\n");
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            fprintf(2, "rm: %s failed to delete\n", argv[i]);
            break;
        }
    }

    exit(0);
}
