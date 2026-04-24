#pragma once

#include "kernel/types.h"
#include "kernel/riscv.h"
#include <span>

namespace xv6 {

struct cpu;
struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
class spinlock;
class sleeplock;
struct stats;
struct superblock;

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

// External symbols from linker script and assembly
extern "C" {
extern char end[];   // first address after kernel
extern char etext[]; // first address after kernel code
extern char trampoline[];
extern char userret[];
extern char uservec[];
void kernelvec();
void swtch(context *, context *);
void start();
void main();
void kerneltrap();
}

// bio.c
void binit();
buf *bread(uint, uint);
void brelse(buf *);
void bwrite(buf *);
void bpin(buf *);
void bunpin(buf *);

// console.c
void consoleinit();
void consoleintr(int);
void consputc(int);

// exec.c
int kexec(const char *, const char **);

// file.c
file *filealloc();
void fileclose(file *);
file *filedup(file *);
void fileinit();
int fileread(file *, uint64, int n);
int filestat(const file *, uint64 addr);
int filewrite(file *, uint64, int n);

// fs.c
void fsinit(int);
int dirlink(inode *, const char *, uint);
inode *dirlookup(inode *, const char *, uint *);
inode *ialloc(uint, short);
inode *idup(inode *);
void iinit();
void ilock(inode *);
void iput(inode *);
void iunlock(inode *);
void iunlockput(inode *);
void iupdate(const inode *);
int namecmp(const char *, const char *);
inode *namei(const char *);
inode *nameiparent(const char *, char *);
uint readi(inode *, int, uint64, uint, uint);
void stati(const inode *, stats *);
int writei(inode *, int, uint64, uint, uint);
void itrunc(inode *);
void ireclaim(int);

// log.c
void initlog(int, const superblock *);
void log_write(buf *);
void begin_op();
void end_op();

// pipe.c
int pipealloc(file **, file **);
void pipeclose(pipe *, int);
int piperead(pipe *, uint64, int);
int pipewrite(pipe *, uint64, int);

// printf.c
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
void panic(const char *) __attribute__((noreturn));
void printfinit();

// proc.c
int cpuid();
void kexit(int);
int kfork();
int growproc(int);
pagetable_t proc_pagetable(proc *);
void proc_freepagetable(pagetable_t, uint64);
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

// swtch.S - already declared above in extern "C"

// spinlock.c
void push_off();
void pop_off();

// string.c
int memcmp(const void *, const void *, uint);
void *memmove(void *, const void *, uint);
void *memset(void *, int, uint);
char *safestrcpy(char *, const char *, int);
int strlen(const char *);
int strncmp(const char *, const char *, uint);
char *strncpy(char *, const char *, int);

// syscall.c
void argint(int, int *);
int argstr(int, char *, int);
void argaddr(int, uint64 *);
int fetchstr(uint64, char *, int);
int fetchaddr(uint64, uint64 *);
void syscall();

// trap.c
extern uint ticks;
void trapinit();
void trapinithart();
extern spinlock tickslock;
void prepare_return();

// uart.c
void uartinit();
void uartintr();
void uartwrite(std::span<char>);
void uartputc_sync(int);
int uartgetc();

// vm.c
pagetable_t kvminit();
void kvminithart();
void kvmmap(pagetable_t, uint64, uint64, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t uvmcreate();
uint64 uvmalloc(pagetable_t, uint64, uint64, int);
uint64 uvmdealloc(pagetable_t, uint64, uint64);
int uvmcopy(pagetable_t, pagetable_t, uint64);
void uvmfree(pagetable_t, uint64);
void uvmunmap(pagetable_t, uint64, uint64, int);
void uvmclear(pagetable_t, uint64);
pte_t *walk(pagetable_t, uint64, int);
uint64 walkaddr(pagetable_t, uint64);
int copyout(pagetable_t, uint64, const char *, uint64);
int copyin(pagetable_t, char *, uint64, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);
int ismapped(pagetable_t, uint64);
uint64 vmfault(pagetable_t, uint64);

// plic.c
void plicinit();
void plicinithart();
int plic_claim();
void plic_complete(int);

// virtio_disk.c
void virtio_disk_init();
void virtio_disk_rw(buf *, int);
void virtio_disk_intr();

} // namespace xv6
