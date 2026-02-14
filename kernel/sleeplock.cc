// Sleeping locks

#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

namespace xv6 {

void initsleeplock(struct sleeplock *lk, const char *name) {
    lk->lk.init_lock("sleep lock");
}

void acquiresleep(struct sleeplock *lk) {
    lk->lk.lock();
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    lk->lk.unlock();
}

void releasesleep(struct sleeplock *lk) {
    lk->lk.lock();
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    lk->lk.unlock();
}

int holdingsleep(struct sleeplock *lk) {

    lk->lk.lock();
    int r = lk->locked && (lk->pid == myproc()->pid);
    lk->lk.unlock();
    return r;
}

} // namespace xv6