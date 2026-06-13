//
// Support functions for system calls that involve file descriptors.
//
#include "kernel/lib/types.h"
#include "kernel/lib/defs.h"
#include "kernel/lib/param.h"
#include "kernel/fs/fs.h"
#include "kernel/sync/spinlock.h"
#include "kernel/fs/file.h"
#include "kernel/fs/stats.h"
#include "kernel/proc/proc.h"
#include "kernel/util/lock_guard.h"

#include <array>

namespace xv6 {

std::array<devsw, NDEV> dev;
struct {
    spinlock lock;
    std::array<file, NFILE> files;
} ftable;

void fileinit() { ftable.lock.init_lock("ftable"); }

// Allocate a file structure.
file *filealloc() {
    {
        util::lock_guard lk(ftable.lock);
        for (file *f = ftable.files.data(); f < ftable.files.data() + NFILE;
             f++) {
            if (f->ref == 0) {
                f->ref = 1;
                return f;
            }
        }
    }
    return nullptr;
}

// Increment ref count for file f.
file *filedup(file *f) {
    {
        util::lock_guard lk(ftable.lock);
        if (f->ref < 1) {
            panic("filedup");
        }
        f->ref++;
    }
    return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(file *f) {
    file ff{};
    {
        util::lock_guard lk(ftable.lock);
        if (f->ref < 1) {
            panic("fileclose");
        }
        if (--f->ref > 0) {
            return;
        }
        ff = *f;
        f->ref = 0;
        f->type = fd_none;
    }

    if (ff.type == fd_pipe) {
        pipeclose(ff.pip, ff.writable);
    } else if (ff.type == fd_inode || ff.type == fd_device) {
        begin_op();
        iput(ff.ip);
        end_op();
    }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(const file *f, const uint64 addr) {
    const proc *p = myproc();
    stats st{};

    if (f->type == fd_inode || f->type == fd_device) {
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
        if (copyout(p->pagetable, addr, reinterpret_cast<char *>(&st),
                    sizeof(st)) < 0) {
            return -1;
        }
        return 0;
    }
    return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(file *f, const uint64 addr, const int n) {
    int r = 0;

    if (f->readable == 0) {
        return -1;
    }

    if (f->type == fd_pipe) {
        r = piperead(f->pip, addr, n);
    } else if (f->type == fd_device) {
        if (f->major < 0 || f->major >= NDEV || !dev[f->major].read) {
            return -1;
        }
        r = dev[f->major].read(1, addr, n);
    } else if (f->type == fd_inode) {
        ilock(f->ip);
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0) {
            f->off += r;
        }
        iunlock(f->ip);
    } else {
        panic("fileread");
    }

    return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(file *f, const uint64 addr, const int n) {
    int ret = 0;

    if (f->writable == 0) {
        return -1;
    }

    if (f->type == fd_pipe) {
        ret = pipewrite(f->pip, addr, n);
    } else if (f->type == fd_device) {
        if (f->major < 0 || f->major >= NDEV || !dev[f->major].write) {
            return -1;
        }
        ret = dev[f->major].write(1, addr, n);
    } else if (f->type == fd_inode) {
        int r;
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, indirect block, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        constexpr int max = (MAXOPBLOCKS - 1 - 1 - 2) / 2 * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max) {
                n1 = max;
            }

            begin_op();
            ilock(f->ip);
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0) {
                f->off += r;
            }
            iunlock(f->ip);
            end_op();

            if (r != n1) {
                // error from writei
                break;
            }
            i += r;
        }
        ret = i == n ? n : -1;
    } else {
        panic("filewrite");
    }

    return ret;
}

} // namespace xv6