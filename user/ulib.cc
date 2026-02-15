#include "kernel/types.h"
#include "kernel/stats.h"
#include "kernel/fcntl.h"
#include "kernel/vm.h"
#include "user/user.h"

using namespace xv6;

//
// wrapper so that it's OK if main() does not call exit().
//
extern "C" void start(const int argc, char **argv) {
    extern int main(int, char **);
    const int r = main(argc, argv);
    exit(r);
}

char *strcpy(char *s, const char *t) {
    char *os = s;
    while ((*s++ = *t++) != 0)
        ;
    return os;
}

int strcmp(const char *p, const char *q) {
    while (*p && *p == *q) {
        p++, q++;
    }
    return static_cast<uchar>(*p) - static_cast<uchar>(*q);
}

uint strlen(const char *s) {
    int n;

    for (n = 0; s[n]; n++)
        ;
    return n;
}

void *memset(void *dst, const int c, const uint n) {
    const auto cdst = static_cast<char *>(dst);
    for (uint i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

char *strchr(const char *s, const char c) {
    for (; *s; s++) {
        if (*s == c) {
            return const_cast<char *>(s);
        }
    }
    return nullptr;
}

char *gets(char *buf, const int max) {
    int i;
    char c;

    for (i = 0; i + 1 < max;) {
        const int cc = read(0, &c, 1);
        if (cc < 1) {
            break;
        }
        buf[i++] = c;
        if (c == '\n' || c == '\r') {
            break;
        }
    }
    buf[i] = '\0';
    return buf;
}

int stat(const char *n, stats *st) {
    const int fd = open(n, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    const int r = fstat(fd, st);
    close(fd);
    return r;
}

int atoi(const char *s) {
    int n = 0;
    while ('0' <= *s && *s <= '9') {
        n = n * 10 + *s++ - '0';
    }
    return n;
}

void *memmove(void *vdst, const void *vsrc, int n) {
    auto dst = static_cast<char *>(vdst);
    if (auto src = static_cast<const char *>(vsrc); src > dst) {
        while (n-- > 0) {
            *dst++ = *src++;
        }
    } else {
        dst += n;
        src += n;
        while (n-- > 0) {
            *--dst = *--src;
        }
    }
    return vdst;
}

int memcmp(const void *s1, const void *s2, uint n) {
    auto p1 = static_cast<const char *>(s1), p2 = static_cast<const char *>(s2);
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void *memcpy(void *dst, const void *src, const int n) {
    return memmove(dst, src, n);
}

char *sbrk(const int n) { return sys_sbrk(n, SBRK_EAGER); }

char *sbrklazy(const int n) { return sys_sbrk(n, SBRK_LAZY); }
