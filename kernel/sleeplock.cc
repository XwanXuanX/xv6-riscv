// Sleeping locks

#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

namespace xv6 {

void initsleeplock(sleeplock *lk) {
    lk->lk.init_lock("sleep lock");
}

void acquiresleep(sleeplock *lk) {
    lk->lk.lock();
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    lk->lk.unlock();
}

void releasesleep(sleeplock *lk) {
    lk->lk.lock();
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    lk->lk.unlock();
}

int holdingsleep(sleeplock *lk) {
    lk->lk.lock();
    const int r = lk->locked && lk->pid == myproc()->pid;
    lk->lk.unlock();
    return r;
}

} // namespace xv6