#pragma once

namespace xv6 {

// syscall.cc
void argint(int, int *);
int argstr(int, char *, int);
void argaddr(int, uint64 *);
int fetchstr(uint64, char *, int);
int fetchaddr(uint64, uint64 *);
void syscall();

} // namespace xv6
