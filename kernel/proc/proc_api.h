#pragma once

#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/mm/pagetable.h"

namespace xv6 {

struct cpu;
struct proc;
class spinlock;

// proc.cc / scheduler.cc
int cpuid();
void kexit(int);
int kfork();
int growproc(int);
pagetable proc_pagetable(proc *);
void proc_freepagetable(pagetable, uint64, uint64, uint64);
int kkill(int);
int killed(proc *);
void setkilled(proc *);
cpu *mycpu();
proc *myproc();
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void sleep(void *, spinlock *);
void userinit();
int kwait(uint64);
void wakeup(const void *);
void yield();
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void procdump();

// exec.cc
int kexec(const char *, const char **);

} // namespace xv6
