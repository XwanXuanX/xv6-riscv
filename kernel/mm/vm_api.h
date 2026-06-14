#pragma once

#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"

namespace xv6 {

struct proc;

// vm.cc
pagetable_t kvminit();
void kvminithart();
void kvmmap(pagetable_t, uint64, uint64, uint64, int);
pagetable_t uvmcreate();
int copyout(pagetable_t, uint64, const char *, uint64);
int copyin(pagetable_t, char *, uint64, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);
uint64 vmfault(pagetable_t, uint64);
int uvmstackshrink(pagetable_t, uint64 *, uint64, uint64);
int try_make_heap_room(proc *, uint64);

} // namespace xv6
