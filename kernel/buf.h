#pragma once

#include "kernel/sleeplock.h"
#include "kernel/fs.h"

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