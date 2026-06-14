#pragma once

#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/mm/pagetable.h"

namespace xv6 {

struct proc;

// vm.cc
pagetable kvminit();
void kvminithart();
void kvmmap(pagetable, uint64, uint64, uint64, int);
int copyout(pagetable, uint64, const char *, uint64);
int copyin(pagetable, char *, uint64, uint64);
int copyinstr(pagetable, char *, uint64, uint64);
uint64 vmfault(pagetable, uint64);
int uvmstackshrink(pagetable, uint64 *, uint64, uint64);
int try_make_heap_room(proc *, uint64);

} // namespace xv6
