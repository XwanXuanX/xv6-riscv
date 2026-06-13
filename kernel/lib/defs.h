#pragma once

#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"

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

} // namespace xv6

#include "kernel/lib/kernel_link.h"
#include "kernel/lib/lib_api.h"
#include "kernel/sync/sync_api.h"
#include "kernel/mm/vm_api.h"
#include "kernel/proc/proc_api.h"
#include "kernel/fs/fs_api.h"
#include "kernel/drivers/drivers_api.h"
#include "kernel/trap/trap.h"
#include "kernel/syscall/syscall_api.h"
