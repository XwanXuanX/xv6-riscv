#pragma once

#include "sleeplock.h"
#include "fs.h"

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
    uchar data[BSIZE];
};

} // namespace xv6