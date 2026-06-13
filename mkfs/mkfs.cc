#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <cassert>
#include <array>
#include <cstdlib>

#define STAT xv6_stat // avoid clash with host struct stat
#include "kernel/lib/types.h"
#include "kernel/fs/fs.h"
#include "kernel/fs/stats.h"
#include "kernel/lib/param.h"

using namespace xv6;

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(a, b)                                                    \
    do {                                                                       \
        switch (0)                                                             \
        case 0:                                                                \
        case (a):;                                                             \
    } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE / BPB + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGBLOCKS + 1; // Header followed by LOGBLOCKS data blocks.
int nmeta;   // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks; // Number of data blocks

int fsfd;
superblock sb;
std::array<char, BSIZE> zeroes;
uint freeinode = 1;
uint freeblock;

void balloc(int);
void wsect(uint, const void *);
void winode(uint, const dinode *);
void rinode(uint inum, dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *xp, int n);
void die(const char *);

// convert to riscv byte order
ushort xshort(const ushort x) {
    ushort y;
    const auto a = reinterpret_cast<uchar *>(&y);
    a[0] = x;
    a[1] = x >> 8;
    return y;
}

uint xint(const uint x) {
    uint y;
    const auto a = reinterpret_cast<uchar *>(&y);
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

int main(const int argc, char *argv[]) {
    int i, cc, fd;
    dirent de{};
    std::array<char, BSIZE> buf{};
    dinode din{};

    STATIC_ASSERT(sizeof(int) == 4, "Integers must be 4 bytes!");

    if (argc < 2) {
        fprintf(stderr, "Usage: mkfs fs.img files...\n");
        exit(1);
    }

    assert((BSIZE % sizeof(dinode)) == 0);
    assert((BSIZE % sizeof(dirent)) == 0);

    fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fsfd < 0) {
        die(argv[1]);
    }

    // 1 fs block = 1 disk sector
    nmeta = 2 + nlog + ninodeblocks + nbitmap;
    nblocks = FSSIZE - nmeta;

    sb.magic = FSMAGIC;
    sb.size = xint(FSSIZE);
    sb.nblocks = xint(nblocks);
    sb.ninodes = xint(NINODES);
    sb.nlog = xint(nlog);
    sb.logstart = xint(2);
    sb.inodestart = xint(2 + nlog);
    sb.bmapstart = xint(2 + nlog + ninodeblocks);

    printf("nmeta %d (boot, super, log blocks %u, inode blocks %u, bitmap "
           "blocks %u) blocks %d total %d\n",
           nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

    freeblock = nmeta; // the first free block that we can allocate

    for (i = 0; i < FSSIZE; i++) {
        wsect(i, zeroes.data());
    }

    memset(buf.data(), 0, sizeof(buf));
    memmove(buf.data(), &sb, sizeof(sb));
    wsect(1, buf.data());

    const uint rootino = ialloc(T_DIR);
    assert(rootino == ROOTINO);

    bzero(&de, sizeof(de));
    de.inum = xshort(rootino);
    strcpy(de.name.data(), ".");
    iappend(rootino, &de, sizeof(de));

    bzero(&de, sizeof(de));
    de.inum = xshort(rootino);
    strcpy(de.name.data(), "..");
    iappend(rootino, &de, sizeof(de));

    for (i = 2; i < argc; i++) {
        // get rid of "user/"
        char *shortname;
        if (strncmp(argv[i], "user/", 5) == 0) {
            shortname = argv[i] + 5;
        } else {
            shortname = argv[i];
        }

        assert(index(shortname, '/') == nullptr);

        if ((fd = open(argv[i], 0)) < 0) {
            die(argv[i]);
        }

        // Skip leading _ in name when writing to file system.
        // The binaries are named _rm, _cat, etc. to keep the
        // build operating system from trying to execute them
        // in place of system binaries like rm and cat.
        if (shortname[0] == '_') {
            shortname += 1;
        }

        assert(strlen(shortname) <= DIRSIZ);

        const uint inum = ialloc(T_FILE);

        bzero(&de, sizeof(de));
        de.inum = xshort(inum);
        strncpy(de.name.data(), shortname, DIRSIZ);
        iappend(rootino, &de, sizeof(de));

        while ((cc = read(fd, buf.data(), sizeof(buf))) > 0) {
            iappend(inum, buf.data(), cc);
        }

        close(fd);
    }

    // fix size of root inode dir
    rinode(rootino, &din);
    uint off = xint(din.size);
    off = (off / BSIZE + 1) * BSIZE;
    din.size = xint(off);
    winode(rootino, &din);

    balloc(static_cast<int>(freeblock));

    exit(0);
}

void wsect(const uint sec, const void *buf) {
    if (lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE) {
        die("lseek");
    }
    if (write(fsfd, buf, BSIZE) != BSIZE) {
        die("write");
    }
}

void winode(const uint inum, const dinode *ip) {
    std::array<char, BSIZE> buf{};

    const uint bn = IBLOCK(inum, sb);
    rsect(bn, buf.data());
    dinode *dip = reinterpret_cast<dinode *>(buf.data()) + inum % IPB;
    *dip = *ip;
    wsect(bn, buf.data());
}

void rinode(const uint inum, dinode *ip) {
    std::array<char, BSIZE> buf{};

    const uint bn = IBLOCK(inum, sb);
    rsect(bn, buf.data());
    const auto dip = reinterpret_cast<dinode *>(buf.data()) + inum % IPB;
    *ip = *dip;
}

void rsect(const uint sec, void *buf) {
    if (lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE) {
        die("lseek");
    }
    if (read(fsfd, buf, BSIZE) != BSIZE) {
        die("read");
    }
}

uint ialloc(const ushort type) {
    const uint inum = freeinode++;
    dinode din{};

    bzero(&din, sizeof(din));
    din.type = xshort(type);
    din.nlink = xshort(1);
    din.size = xint(0);
    winode(inum, &din);
    return inum;
}

void balloc(const int used) {
    std::array<uchar, BSIZE> buf{};

    printf("balloc: first %d blocks have been allocated\n", used);
    assert(used < BPB);
    bzero(buf.data(), BSIZE);
    for (int i = 0; i < used; i++) {
        buf[i / 8] = buf[i / 8] | 0x1 << (i % 8);
    }
    printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
    wsect(sb.bmapstart, buf.data());
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void iappend(const uint inum, void *xp, int n) {
    const char *p = static_cast<char *>(xp);
    dinode din{};
    std::array<char, BSIZE> buf{};
    std::array<uint, NINDIRECT> indirect{};
    uint x;

    rinode(inum, &din);
    uint off = xint(din.size);
    // printf("append inum %d at off %d sz %d\n", inum, off, n);
    while (n > 0) {
        const uint fbn = off / BSIZE;
        assert(fbn < MAXFILE);
        if (fbn < NDIRECT) {
            if (xint(din.addrs[fbn]) == 0) {
                din.addrs[fbn] = xint(freeblock++);
            }
            x = xint(din.addrs[fbn]);
        } else {
            if (xint(din.addrs[NDIRECT]) == 0) {
                din.addrs[NDIRECT] = xint(freeblock++);
            }
            rsect(xint(din.addrs[NDIRECT]), indirect.data());
            if (indirect[fbn - NDIRECT] == 0) {
                indirect[fbn - NDIRECT] = xint(freeblock++);
                wsect(xint(din.addrs[NDIRECT]), indirect.data());
            }
            x = xint(indirect[fbn - NDIRECT]);
        }
        const int n1 = MIN(n, (fbn + 1) * BSIZE - off);
        rsect(x, buf.data());
        bcopy(p, buf.data() + off - fbn * BSIZE, n1);
        wsect(x, buf.data());
        n -= n1;
        off += n1;
        p += n1;
    }
    din.size = xint(off);
    winode(inum, &din);
}

void die(const char *s) {
    perror(s);
    exit(1);
}
