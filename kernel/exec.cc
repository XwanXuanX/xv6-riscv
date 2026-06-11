#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/riscv.h"
#include "kernel/proc.h"
#include "kernel/defs.h"
#include "kernel/elf.h"
#include "kernel/memlayout.h"
#include <array>

namespace xv6 {

static int loadseg(pde_t *, uint64, inode *, uint, uint);

// map ELF permissions to PTE permission bits.
int flags2_perm(const int flags) {
    int perm = 0;
    if (flags & 0x1) {
        perm = PTE_X;
    }
    if (flags & 0x2) {
        perm |= PTE_W;
    }
    return perm;
}

//
// the implementation of the exec() system call
//
int kexec(const char *path, const char **argv) {
    const char *s, *last;
    int i, off;
    std::array<uint64, MAXARG> ustack{};
    uint64 argc, sz = 0, sp, stackbase;
    uint64 sz1, old_heap_top, old_stack_bottom, old_stack_top;
    uint64 new_heap_top = 0, new_stack_bottom = 0, new_stack_top = 0;
    bool stack_mapped = false;
    elfhdr elf{};
    inode *ip;
    proghdr ph{};
    pagetable_t pagetable = nullptr, oldpagetable;
    proc *p = myproc();

    begin_op();

    // Open the executable file.
    if ((ip = namei(path)) == nullptr) {
        end_op();
        return -1;
    }
    ilock(ip);

    // Read the ELF header.
    if (readi(ip, 0, reinterpret_cast<uint64>(&elf), 0, sizeof(elf)) !=
        sizeof(elf)) {
        goto bad;
    }

    // Is this really an ELF file?
    if (elf.magic != ELF_MAGIC) {
        goto bad;
    }

    if ((pagetable = proc_pagetable(p)) == nullptr) {
        goto bad;
    }

    // Load program into memory.
    // User program memory layout:
    // clang-format off
    // Low VA                                                                                  High VA
    // ┌──────────────┬──────────────────┬──────────┬───────────┬────────────┬───────────┬────────────┐
    // │ text/data    │ heap             │   gap    │ guard     │ stack      │ TRAPFRAME │ TRAMPOLINE │
    // │ (ELF)        │ (sbrk, grows up) │ unmapped │ (no PTE_U)│ (fixed)    │ trap save │ trap code  │
    // └──────────────┴──────────────────┴──────────┴───────────┴────────────┴───────────┴────────────┘
    // 0              heap_top (= PGROUNDUP(sz))   guard_va  stack_bottom  USERSTACK_HIGH
    // clang-format on
    for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if (readi(ip, 0, reinterpret_cast<uint64>(&ph), off, sizeof(ph)) !=
            sizeof(ph)) {
            goto bad;
        }
        if (ph.type != ELF_PROG_LOAD) {
            continue;
        }
        if (ph.memsz < ph.filesz) {
            goto bad;
        }
        if (ph.vaddr + ph.memsz < ph.vaddr) {
            goto bad;
        }
        if (ph.vaddr % PGSIZE != 0) {
            goto bad;
        }
        if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz,
                            flags2_perm(ph.flags))) == 0) {
            goto bad;
        }
        sz = sz1;
        if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0) {
            goto bad;
        }
    }
    iunlockput(ip);
    end_op();
    ip = nullptr;

    p = myproc();
    // old_sz is used to free the old page table after the new page table is
    // committed, now contains two sections:
    // 1. the old text/data/bss + heap section
    // 2. the old stack section
    old_heap_top = p->heap_top;
    old_stack_bottom = p->stack_bottom;
    old_stack_top = p->stack_top;

    // Allocate guard + initial user stack pages just below TRAPFRAME.
    sz = PGROUNDUP(sz);
    new_heap_top = sz;
    new_stack_top = USERSTACK_HIGH;
    new_stack_bottom = USERSTACK_HIGH - USERSTACK * PGSIZE;
    if (uvmalloc(pagetable, new_stack_bottom, new_stack_top, PTE_W) == 0) {
        goto bad;
    }
    stack_mapped = true;
    sp = new_stack_top;
    stackbase = new_stack_bottom;

    // Copy argument strings into new stack, remember their
    // addresses in ustack[].
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG) {
            goto bad;
        }
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16; // riscv sp must be 16-byte aligned
        if (sp < stackbase) {
            goto bad;
        }
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0) {
            goto bad;
        }
        ustack[argc] = sp;
    }
    ustack[argc] = 0;

    // push a copy of ustack[], the array of argv[] pointers.
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16;
    if (sp < stackbase) {
        goto bad;
    }
    if (copyout(pagetable, sp, reinterpret_cast<char *>(ustack.data()),
                (argc + 1) * sizeof(uint64)) < 0) {
        goto bad;
    }

    // a0 and a1 contain arguments to user main(argc, argv)
    // argc is returned via the system call return
    // value, which goes in a0.
    p->trapf->a1 = sp;

    // Save program name for debugging.
    for (last = s = path; *s; s++) {
        if (*s == '/') {
            last = s + 1;
        }
    }
    safestrcpy(p->name.data(), last, sizeof(p->name));

    // Commit to the user image.
    oldpagetable = p->pagetable;
    p->pagetable = pagetable;
    p->heap_top = new_heap_top;
    p->heap_bottom = new_heap_top;
    p->stack_bottom = new_stack_bottom;
    p->stack_top = new_stack_top;
    p->trapf->epc = elf.entry; // initial program counter = ulib.c:start()
    p->trapf->sp = sp;         // initial stack pointer
    proc_freepagetable(oldpagetable, old_heap_top, old_stack_bottom,
                       old_stack_top);

    return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
    if (pagetable) {
        proc_freepagetable(pagetable, PGROUNDUP(sz),
                           stack_mapped ? new_stack_bottom : 0,
                           stack_mapped ? new_stack_top : 0);
    }
    if (ip) {
        iunlockput(ip);
        end_op();
    }
    return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int loadseg(pagetable_t pagetable, const uint64 va, inode *ip,
                   const uint offset, const uint sz) {
    uint n;

    for (uint i = 0; i < sz; i += PGSIZE) {
        const uint64 pa = walkaddr(pagetable, va + i);
        if (pa == 0) {
            panic("loadseg: address should exist");
        }
        if (sz - i < PGSIZE) {
            n = sz - i;
        } else {
            n = PGSIZE;
        }
        if (readi(ip, 0, pa, offset + i, n) != n) {
            return -1;
        }
    }

    return 0;
}

} // namespace xv6