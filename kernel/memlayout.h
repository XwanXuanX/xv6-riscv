// Physical memory layout
#pragma once

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0
// 10001000 -- virtio disk
// 80000000 -- qemu's boot ROM loads the kernel here,
//             then jumps here.
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart) * 0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128 * 1024 * 1024) // 0x88000000, 32768 frames

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// Kernel stack slots live in a high VA region directly below TRAMPOLINE.
// Each slot is two pages: guard (unmapped) then stack (mapped on demand).
// Slot index 0 is closest to TRAMPOLINE.
#define KSTACK_SLOT_PAGES 2
#define KSTACK_SLOT_SIZE (KSTACK_SLOT_PAGES * PGSIZE)
// 64 MiB of VA for kernel stack slots (32768 two-page slots).
#define KSTACK_VA_REGION_SIZE (64UL * 1024 * 1024)
#define KSTACK_VA_FLOOR (TRAMPOLINE - KSTACK_VA_REGION_SIZE)

// Stack page VA for slot index (0-based, allocated from low index upward).
#define KSTACK_SLOT_VA(index) (TRAMPOLINE - ((index) + 1ULL) * KSTACK_SLOT_SIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
