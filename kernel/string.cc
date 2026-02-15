#include "types.h"

namespace xv6 {

void *
memset(void *dst, const int c, const uint n) {
    const auto cdst = static_cast<char *>(dst);
    for (uint i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

int memcmp(const void *v1, const void *v2, uint n) {

    auto s1 = static_cast<const uchar *>(v1);
    auto s2 = static_cast<const uchar *>(v2);
    while (n-- > 0) {
        if (*s1 != *s2)
            return *s1 - *s2;
        s1++, s2++;
    }

    return 0;
}

void *
memmove(void *dst, const void *src, uint n) {

    if (n == 0)
        return dst;

    auto s = static_cast<const char *>(src);
    auto d = static_cast<char *>(dst);
    if (s < d && s + n > d) {
        s += n;
        d += n;
        while (n-- > 0)
            *--d = *--s;
    } else
        while (n-- > 0)
            *d++ = *s++;

    return dst;
}

// memcpy exists to placate GCC.  Use memmove.
void *
memcpy(void *dst, const void *src, const uint n) {
    return memmove(dst, src, n);
}

int strncmp(const char *p, const char *q, uint n) {
    while (n > 0 && *p && *p == *q)
        n--, p++, q++;
    if (n == 0)
        return 0;
    return static_cast<uchar>(*p) - static_cast<uchar>(*q);
}

char *
strncpy(char *s, const char *t, int n) {

    char *os = s;
    while (n-- > 0 && (*s++ = *t++) != 0)
        ;
    while (n-- > 0)
        *s++ = 0;
    return os;
}

// Like strncpy but guaranteed to NUL-terminate.
char *
safestrcpy(char *s, const char *t, int n) {

    char *os = s;
    if (n <= 0)
        return os;
    while (--n > 0 && (*s++ = *t++) != 0)
        ;
    *s = 0;
    return os;
}

int strlen(const char *s) {
    int n;

    for (n = 0; s[n]; n++)
        ;
    return n;
}

} // namespace xv6

// Global aliases for compiler-generated calls
extern "C" {
void *memset(void *dst, const int c, const uint n) {
    return xv6::memset(dst, c, n);
}

void *memmove(void *dst, const void *src, const uint n) {
    return xv6::memmove(dst, src, n);
}

int memcmp(const void *v1, const void *v2, const uint n) {
    return xv6::memcmp(v1, v2, n);
}
}
