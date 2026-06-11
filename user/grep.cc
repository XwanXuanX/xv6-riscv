// Simple grep.  Only supports ^ . * $ operators.

#include "kernel/fcntl.h"
#include "user/user.h"

#include <array>
#include <span>

std::array<char, 1024> buf;
int match(char *, char *);

void grep(char *pattern, const int fd) {
    int n;
    char *q;

    int m = 0;
    while ((n = read(fd, buf.data() + m, sizeof(buf) - m - 1)) > 0) {
        m += n;
        buf[m] = '\0';
        char *p = buf.data();
        while ((q = strchr(p, '\n')) != nullptr) {
            *q = 0;
            if (match(pattern, p)) {
                *q = '\n';
                write(1, p, q + 1 - p);
            }
            p = q + 1;
        }
        if (m > 0) {
            m -= p - buf.data();
            memmove(buf.data(), p, m);
        }
    }
}

int main(const int argc, const std::span<char *> argv) {
    int fd;

    if (argc <= 1) {
        fprintf(2, "usage: grep pattern [file ...]\n");
        exit(1);
    }
    char *pattern = argv[1];

    if (argc <= 2) {
        grep(pattern, 0);
        exit(0);
    }

    for (int i = 2; i < argc; i++) {
        if ((fd = open(argv[i], O_RDONLY)) < 0) {
            printf("grep: cannot open %s\n", argv[i]);
            exit(1);
        }
        grep(pattern, fd);
        close(fd);
    }
    exit(0);
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9, or
// https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html

int matchhere(char *, char *);
int matchstar(int, char *, char *);

int match(char *re, char *text) {
    if (re[0] == '^') {
        return matchhere(re + 1, text);
    }
    do { // must look at empty string
        if (matchhere(re, text)) {
            return 1;
        }
    } while (*text++ != '\0');
    return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text) {
    if (re[0] == '\0') {
        return 1;
    }
    if (re[1] == '*') {
        return matchstar(re[0], re + 2, text);
    }
    if (re[0] == '$' && re[1] == '\0') {
        return *text == '\0';
    }
    if (*text != '\0' && (re[0] == '.' || re[0] == *text)) {
        return matchhere(re + 1, text + 1);
    }
    return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(const int c, char *re, char *text) {
    do { // a * matches zero or more instances
        if (matchhere(re, text)) {
            return 1;
        }
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}
