#pragma once

namespace xv6 {

struct buf;
struct file;
struct inode;
struct pipe;
struct stats;
struct superblock;

// bio.cc
void binit();
buf *bread(uint, uint);
void brelse(buf *);
void bwrite(buf *);
void bpin(buf *);
void bunpin(buf *);

// file.cc
file *filealloc();
void fileclose(file *);
file *filedup(file *);
void fileinit();
int fileread(file *, uint64, int n);
int filestat(const file *, uint64 addr);
int filewrite(file *, uint64, int n);

// fs.cc
void fsinit(int);
int dirlink(inode *, const char *, uint);
inode *dirlookup(inode *, const char *, uint *);
inode *ialloc(uint, short);
inode *idup(inode *);
void iinit();
void ilock(inode *);
void iput(inode *);
void iunlock(inode *);
void iunlockput(inode *);
void iupdate(const inode *);
int namecmp(const char *, const char *);
inode *namei(const char *);
inode *nameiparent(const char *, char *);
uint readi(inode *, int, uint64, uint, uint);
void stati(const inode *, stats *);
int writei(inode *, int, uint64, uint, uint);
void itrunc(inode *);
void ireclaim(int);

// log.cc
void initlog(int, const superblock *);
void log_write(buf *);
void begin_op();
void end_op();

// pipe.cc
int pipealloc(file **, file **);
void pipeclose(pipe *, int);
int piperead(pipe *, uint64, int);
int pipewrite(pipe *, uint64, int);

} // namespace xv6
