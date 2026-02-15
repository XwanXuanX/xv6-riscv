//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "file.h"
#include "stats.h"
#include "proc.h"

namespace xv6 {

struct devsw devsw[NDEV];
struct {
    spinlock lock;
    file files[NFILE];
} ftable;

void fileinit() {
    ftable.lock.init_lock("ftable");
}

// Allocate a file structure.
file *
filealloc() {
    ftable.lock.lock();
    for (file *f = ftable.files; f < ftable.files + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            ftable.lock.unlock();
            return f;
        }
    }
    ftable.lock.unlock();
    return nullptr;
}

// Increment ref count for file f.
file *
filedup(file *f) {
    ftable.lock.lock();
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    ftable.lock.unlock();
    return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(file *f) {
    ftable.lock.lock();
    if (f->ref < 1)
        panic("fileclose");
    if (--f->ref > 0) {
        ftable.lock.unlock();
        return;
    }
    const file ff = *f;
    f->ref = 0;
    f->type = FD_NONE;
    ftable.lock.unlock();

    if (ff.type == FD_PIPE) {
        pipeclose(ff.pip, ff.writable);
    } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
        begin_op();
        iput(ff.ip);
        end_op();
    }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(file *f, const uint64 addr) {
    const proc *p = myproc();
    stats st{};

    if (f->type == FD_INODE || f->type == FD_DEVICE) {
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
        if (copyout(p->pagetable, addr, reinterpret_cast<char *>(&st), sizeof(st)) < 0)
            return -1;
        return 0;
    }
    return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(file *f, const uint64 addr, const int n) {
    int r = 0;

    if (f->readable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        r = piperead(f->pip, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
            return -1;
        r = devsw[f->major].read(1, addr, n);
    } else if (f->type == FD_INODE) {
        ilock(f->ip);
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
            f->off += r;
        iunlock(f->ip);
    } else {
        panic("fileread");
    }

    return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(file *f, const uint64 addr, const int n) {
    int r, ret = 0;

    if (f->writable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        ret = pipewrite(f->pip, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
            return -1;
        ret = devsw[f->major].write(1, addr, n);
    } else if (f->type == FD_INODE) {
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, indirect block, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        const int max = (MAXOPBLOCKS - 1 - 1 - 2) / 2 * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max)
                n1 = max;

            begin_op();
            ilock(f->ip);
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
                f->off += r;
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