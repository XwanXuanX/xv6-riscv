// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
#include "kernel/lib/types.h"
#include "kernel/lib/param.h"
#include "kernel/sync/spinlock.h"
#include "kernel/lib/defs.h"
#include "kernel/fs/buf.h"

#include <array>

namespace xv6 {

struct {
    spinlock lock;
    std::array<buf, NBUF> buffer;

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    buf head;
} bcache;

void binit() {
    bcache.lock.init_lock("bcache");

    // Create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (buf *b = bcache.buffer.data(); b < bcache.buffer.data() + NBUF; b++) {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        b->lock.init_lock();
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static buf *bget(const uint dev, const uint blockno) {
    buf *b;

    bcache.lock.lock();

    // Is the block already cached?
    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            bcache.lock.unlock();
            b->lock.lock();
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            bcache.lock.unlock();
            b->lock.lock();
            return b;
        }
    }
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
buf *bread(const uint dev, const uint blockno) {
    buf *b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(buf *b) {
    if (!b->lock.holding()) {
        panic("bwrite");
    }
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(buf *b) {
    if (!b->lock.holding()) {
        panic("brelse");
    }

    b->lock.unlock();

    bcache.lock.lock();
    b->refcnt--;
    if (b->refcnt == 0) {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    bcache.lock.unlock();
}

void bpin(buf *b) {
    bcache.lock.lock();
    b->refcnt++;
    bcache.lock.unlock();
}

void bunpin(buf *b) {
    bcache.lock.lock();
    b->refcnt--;
    bcache.lock.unlock();
}

} // namespace xv6