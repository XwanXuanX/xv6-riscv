#include "kernel/fcntl.h"
#include "user/user.h"

#include <array>
#include <span>

std::array<char, 512> buf;

void cat(const int fd) {
    int n;

    while ((n = read(fd, buf.data(), sizeof(buf))) > 0) {
        if (write(1, buf.data(), n) != n) {
            fprintf(2, "cat: write error\n");
            exit(1);
        }
    }
    if (n < 0) {
        fprintf(2, "cat: read error\n");
        exit(1);
    }
}

int main(const int argc, const std::span<char *> argv) {
    int fd;

    if (argc <= 1) {
        cat(0);
        exit(0);
    }

    for (int i = 1; i < argc; i++) {
        if ((fd = open(argv[i], O_RDONLY)) < 0) {
            fprintf(2, "cat: cannot open %s\n", argv[i]);
            exit(1);
        }
        cat(fd);
        close(fd);
    }
    exit(0);
}
