#pragma once

#include "kernel/lib/types.h"

#include <span>

namespace xv6 {

// string.cc
int memcmp(const void *, const void *, uint);
void *memmove(void *, const void *, uint);
void *memset(void *, int, uint);
char *safestrcpy(char *, const char *, int);
int strlen(const char *);
int strncmp(const char *, const char *, uint);
char *strncpy(char *, const char *, int);

// printf.cc
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *) __attribute__((noreturn));
void printfinit();

} // namespace xv6
