#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"
#include <array>

namespace xv6 {

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
    int n;
    std::array<uint, LOGBLOCKS> block;
};

struct log {
    spinlock lock;
    int start;
    int outstanding; // how many FS sys calls are executing.
    int committing;  // in commit(), please wait.
    int dev;
    logheader lh;
};
log logger;

static void recover_from_log();
static void commit();

void initlog(const int dev, const superblock *sb) {
    if constexpr (sizeof(logheader) >= BSIZE) {
        panic("initlog: too big logheader");
    }

    logger.lock.init_lock("log");
    logger.start = sb->logstart;
    logger.dev = dev;
    recover_from_log();
}

// Copy committed blocks from log to their home location
static void install_trans(const int recovering) {
    for (int tail = 0; tail < logger.lh.n; tail++) {
        if (recovering) {
            printf("recovering tail %d dst %d\n", tail, logger.lh.block[tail]);
        }
        buf *lbuf =
            bread(logger.dev, logger.start + tail + 1);       // read log block
        buf *dbuf = bread(logger.dev, logger.lh.block[tail]); // read dst
        memmove(dbuf->data.data(), lbuf->data.data(),
                BSIZE); // copy block to dst
        bwrite(dbuf);   // write dst to disk
        if (recovering == 0) {
            bunpin(dbuf);
        }
        brelse(lbuf);
        brelse(dbuf);
    }
}

// Read the log header from disk into the in-memory log header
static void read_head() {
    buf *buf = bread(logger.dev, logger.start);
    const logheader *lh = reinterpret_cast<logheader *>(buf->data.data());
    logger.lh.n = lh->n;
    for (int i = 0; i < logger.lh.n; i++) {
        logger.lh.block[i] = lh->block[i];
    }
    brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void write_head() {
    buf *buf = bread(logger.dev, logger.start);
    const auto hb = reinterpret_cast<logheader *>(buf->data.data());
    hb->n = logger.lh.n;
    for (int i = 0; i < logger.lh.n; i++) {
        hb->block[i] = logger.lh.block[i];
    }
    bwrite(buf);
    brelse(buf);
}

static void recover_from_log() {
    read_head();
    install_trans(1); // if committed, copy from log to disk
    logger.lh.n = 0;
    write_head(); // clear the log
}

// called at the start of each FS system call.
void begin_op() {
    logger.lock.lock();
    while (true) {
        if (logger.committing) {
            sleep(&logger, &logger.lock);
        } else if (logger.lh.n + (logger.outstanding + 1) * MAXOPBLOCKS >
                   LOGBLOCKS) {
            // this op might exhaust log space; wait for commit.
            sleep(&logger, &logger.lock);
        } else {
            logger.outstanding += 1;
            logger.lock.unlock();
            break;
        }
    }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op() {
    int do_commit = 0;

    logger.lock.lock();
    logger.outstanding -= 1;
    if (logger.committing) {
        panic("log.committing");
    }
    if (logger.outstanding == 0) {
        do_commit = 1;
        logger.committing = 1;
    } else {
        // begin_op() may be waiting for log space,
        // and decrementing log.outstanding has decreased
        // the amount of reserved space.
        wakeup(&logger);
    }
    logger.lock.unlock();

    if (do_commit) {
        // call commit w/o holding locks, since not allowed
        // to sleep with locks.
        commit();
        logger.lock.lock();
        logger.committing = 0;
        wakeup(&logger);
        logger.lock.unlock();
    }
}

// Copy modified blocks from cache to log.
static void write_log() {
    for (int tail = 0; tail < logger.lh.n; tail++) {
        buf *to = bread(logger.dev, logger.start + tail + 1); // log block
        buf *from = bread(logger.dev, logger.lh.block[tail]); // cache block
        memmove(to->data.data(), from->data.data(), BSIZE);
        bwrite(to); // write the log
        brelse(from);
        brelse(to);
    }
}

static void commit() {
    if (logger.lh.n > 0) {
        write_log();      // Write modified blocks from cache to log
        write_head();     // Write header to disk -- the real commit
        install_trans(0); // Now install writes to home locations
        logger.lh.n = 0;
        write_head(); // Erase the transaction from the log
    }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(buf *b) {
    int i;

    logger.lock.lock();
    if (logger.lh.n >= LOGBLOCKS) {
        panic("too big a transaction");
    }
    if (logger.outstanding < 1) {
        panic("log_write outside of trans");
    }

    for (i = 0; i < logger.lh.n; i++) {
        if (logger.lh.block[i] == b->blockno) { // log absorption
            break;
        }
    }
    logger.lh.block[i] = b->blockno;
    if (i == logger.lh.n) { // Add new block to log?
        bpin(b);
        logger.lh.n++;
    }
    logger.lock.unlock();
}

} // namespace xv6