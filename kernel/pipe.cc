#include "types.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "file.h"

namespace xv6 {

#define PIPESIZE 512

struct pipe {
    spinlock lock;
    char data[PIPESIZE];
    uint nread;    // number of bytes read
    uint nwrite;   // number of bytes written
    int readopen;  // read fd is still open
    int writeopen; // write fd is still open
};

int pipealloc(file **f0, file **f1) {
    pipe *pi = nullptr;
    *f0 = *f1 = nullptr;
    if ((*f0 = filealloc()) == nullptr || (*f1 = filealloc()) == nullptr)
        goto bad;
    if ((pi = static_cast<pipe *>(kalloc())) == nullptr)
        goto bad;
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;
    pi->lock.init_lock("pipe");
    (*f0)->type = fd_pipe;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pip = pi;
    (*f1)->type = fd_pipe;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pip = pi;
    return 0;

bad:
    if (pi)
        kfree(pi);
    if (*f0)
        fileclose(*f0);
    if (*f1)
        fileclose(*f1);
    return -1;
}

void pipeclose(pipe *pi, const int writable) {
    pi->lock.lock();
    if (writable) {
        pi->writeopen = 0;
        wakeup(&pi->nread);
    } else {
        pi->readopen = 0;
        wakeup(&pi->nwrite);
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        pi->lock.unlock();
        kfree(pi);
    } else
        pi->lock.unlock();
}

int pipewrite(pipe *pi, const uint64 addr, const int n) {
    int i = 0;
    proc *pr = myproc();

    pi->lock.lock();
    while (i < n) {
        if (pi->readopen == 0 || killed(pr)) {
            pi->lock.unlock();
            return -1;
        }
        if (pi->nwrite == pi->nread + PIPESIZE) { // DOC: pipewrite-full
            wakeup(&pi->nread);
            sleep(&pi->nwrite, &pi->lock);
        } else {
            char ch;
            if (copyin(pr->pagetable, &ch, addr + i, 1) == -1)
                break;
            pi->data[pi->nwrite++ % PIPESIZE] = ch;
            i++;
        }
    }
    wakeup(&pi->nread);
    pi->lock.unlock();

    return i;
}

int piperead(pipe *pi, const uint64 addr, const int n) {
    int i;
    proc *pr = myproc();
    char ch;

    pi->lock.lock();
    while (pi->nread == pi->nwrite && pi->writeopen) { // DOC: pipe-empty
        if (killed(pr)) {
            pi->lock.unlock();
            return -1;
        }
        sleep(&pi->nread, &pi->lock); // DOC: piperead-sleep
    }
    for (i = 0; i < n; i++) { // DOC: piperead-copy
        if (pi->nread == pi->nwrite)
            break;
        ch = pi->data[pi->nread % PIPESIZE];
        if (copyout(pr->pagetable, addr + i, &ch, 1) == -1) {
            if (i == 0)
                i = -1;
            break;
        }
        pi->nread++;
    }
    wakeup(&pi->nwrite); // DOC: piperead-wakeup
    pi->lock.unlock();
    return i;
}

} // namespace xv6