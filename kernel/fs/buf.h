#pragma once

#include "kernel/sync/sleeplock.h"
#include "kernel/fs/fs.h"

#include <array>

namespace xv6 {

struct buf {
    int valid; // has data been read from disk?
    int disk;  // does disk "own" buf?
    uint dev;
    uint blockno;
    sleeplock lock;
    uint refcnt;
    buf *prev; // LRU cache list
    buf *next;
    std::array<uchar, BSIZE> data;
};

} // namespace xv6