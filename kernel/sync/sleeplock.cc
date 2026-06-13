// Sleeping locks
#include "kernel/proc/proc_api.h"
#include "kernel/proc/proc.h"
#include "kernel/sync/sleeplock.h"
#include "kernel/util/lock_guard.h"

namespace xv6 {

void sleeplock::init_lock() { lk_.init_lock("sleep lock"); }

void sleeplock::lock() {
    lk_.lock();
    while (locked_) {
        sleep(this, &lk_);
    }
    locked_ = 1;
    pid_ = myproc()->pid;
    lk_.unlock();
}

void sleeplock::unlock() {
    lk_.lock();
    locked_ = 0;
    pid_ = 0;
    wakeup(this);
    lk_.unlock();
}

bool sleeplock::holding() {
    const int r = [this] {
        util::lock_guard lk(lk_);
        return locked_ && pid_ == myproc()->pid;
    }();
    return r;
}

} // namespace xv6