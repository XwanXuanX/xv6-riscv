#pragma once

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/sleeplock.h"
#include <array>

namespace xv6 {

struct pipe;

enum file_type { fd_none, fd_pipe, fd_inode, fd_device };

// in-memory copy of an inode
struct inode {
    uint dev;       // Device number
    uint inum;      // Inode number
    int ref;        // Reference count
    sleeplock lock; // protects everything below here
    int valid;      // inode has been read from disk?

    short type; // copy of disk inode
    short major;
    short minor;
    short nlink;
    uint size;
    std::array<uint, NDIRECT + 1> addrs;
};

struct file {
    file_type type;
    int ref; // reference count
    char readable;
    char writable;
    pipe *pip;   // FD_PIPE
    inode *ip;   // FD_INODE and FD_DEVICE
    uint off;    // FD_INODE
    short major; // FD_DEVICE
};

#define MAJOR(dev) ((dev) >> 16 & 0xFFFF)
#define MINOR(dev) ((dev) & 0xFFFF)
#define MKDEV(m, n) ((uint)((m) << 16 | (n)))

// map major device number to device functions.
struct devsw {
    int (*read)(int, uint64, int);
    int (*write)(int, uint64, int);
};

extern std::array<devsw, NDEV> dev;

#define CONSOLE 1

} // namespace xv6