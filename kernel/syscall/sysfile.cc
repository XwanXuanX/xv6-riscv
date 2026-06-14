//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//
#include "kernel/lib/types.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/lib/defs.h"
#include "kernel/lib/param.h"
#include "kernel/fs/stats.h"
#include "kernel/proc/proc.h"
#include "kernel/fs/fs.h"
#include "kernel/fs/file.h"
#include "kernel/fs/fcntl.h"
#include "kernel/mm/page_allocator.h"

#include <array>

namespace xv6 {

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(const int n, int *pfd, file **pf) {
    int fd;
    file *f;

    argint(n, &fd);
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == nullptr) {
        return -1;
    }
    if (pfd) {
        *pfd = fd;
    }
    if (pf) {
        *pf = f;
    }
    return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(file *f) {
    proc *p = myproc();

    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd] == nullptr) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

uint64 sys_dup() {
    file *f;
    int fd;

    if (argfd(0, nullptr, &f) < 0) {
        return -1;
    }
    if ((fd = fdalloc(f)) < 0) {
        return -1;
    }
    filedup(f);
    return fd;
}

uint64 sys_read() {
    file *f;
    int n;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if (argfd(0, nullptr, &f) < 0) {
        return -1;
    }
    return fileread(f, p, n);
}

uint64 sys_write() {
    file *f;
    int n;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if (argfd(0, nullptr, &f) < 0) {
        return -1;
    }

    return filewrite(f, p, n);
}

uint64 sys_close() {
    int fd;
    file *f;

    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    myproc()->ofile[fd] = nullptr;
    fileclose(f);
    return 0;
}

uint64 sys_fstat() {
    file *f;
    uint64 st; // user pointer to struct stat

    argaddr(1, &st);
    if (argfd(0, nullptr, &f) < 0) {
        return -1;
    }
    return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link() {
    std::array<char, DIRSIZ> name{};
    std::array<char, MAXPATH> nw{}, old{};
    inode *dp, *ip;

    if (argstr(0, old.data(), MAXPATH) < 0 ||
        argstr(1, nw.data(), MAXPATH) < 0) {
        return -1;
    }

    begin_op();
    if ((ip = namei(old.data())) == nullptr) {
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->type == T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }

    ip->nlink++;
    iupdate(ip);
    iunlock(ip);

    if ((dp = nameiparent(nw.data(), name.data())) == nullptr) {
        goto bad;
    }
    ilock(dp);
    if (dp->dev != ip->dev || dirlink(dp, name.data(), ip->inum) < 0) {
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);

    end_op();

    return 0;

bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(inode *dp) {
    dirent de{};

    for (uint off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
            panic("isdirempty: readi");
        }
        if (de.inum != 0) {
            return 0;
        }
    }
    return 1;
}

uint64 sys_unlink() {
    inode *ip, *dp;
    dirent de{};
    std::array<char, DIRSIZ> name{};
    std::array<char, MAXPATH> path{};
    uint off;

    if (argstr(0, path.data(), MAXPATH) < 0) {
        return -1;
    }

    begin_op();
    if ((dp = nameiparent(path.data(), name.data())) == nullptr) {
        end_op();
        return -1;
    }

    ilock(dp);

    // Cannot unlink "." or "..".
    if (namecmp(name.data(), ".") == 0 || namecmp(name.data(), "..") == 0) {
        goto bad;
    }

    if ((ip = dirlookup(dp, name.data(), &off)) == nullptr) {
        goto bad;
    }
    ilock(ip);

    if (ip->nlink < 1) {
        panic("unlink: nlink < 1");
    }
    if (ip->type == T_DIR && !isdirempty(ip)) {
        iunlockput(ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("unlink: writei");
    }
    if (ip->type == T_DIR) {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);

    end_op();

    return 0;

bad:
    iunlockput(dp);
    end_op();
    return -1;
}

static inode *create(const char *path, const short type, const short major,
                     const short minor) {
    inode *ip, *dp;
    std::array<char, DIRSIZ> name{};

    if ((dp = nameiparent(path, name.data())) == nullptr) {
        return nullptr;
    }

    ilock(dp);

    if ((ip = dirlookup(dp, name.data(), nullptr)) != nullptr) {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE)) {
            return ip;
        }
        iunlockput(ip);
        return nullptr;
    }

    if ((ip = ialloc(dp->dev, type)) == nullptr) {
        iunlockput(dp);
        return nullptr;
    }

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR) { // Create . and .. entries.
        // No ip->nlink++ for ".": avoid cyclic ref count.
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) {
            goto fail;
        }
    }

    if (dirlink(dp, name.data(), ip->inum) < 0) {
        goto fail;
    }

    if (type == T_DIR) {
        // now that success is guaranteed:
        dp->nlink++; // for ".."
        iupdate(dp);
    }

    iunlockput(dp);

    return ip;

fail:
    // something went wrong. de-allocate ip.
    ip->nlink = 0;
    iupdate(ip);
    iunlockput(ip);
    iunlockput(dp);
    return nullptr;
}

uint64 sys_open() {
    std::array<char, MAXPATH> path{};
    int fd, omode;
    file *f;
    inode *ip;

    argint(1, &omode);
    if (argstr(0, path.data(), MAXPATH) < 0) {
        return -1;
    }

    begin_op();

    if (omode & O_CREATE) {
        ip = create(path.data(), T_FILE, 0, 0);
        if (ip == nullptr) {
            end_op();
            return -1;
        }
    } else {
        if ((ip = namei(path.data())) == nullptr) {
            end_op();
            return -1;
        }
        ilock(ip);
        if (ip->type == T_DIR && omode != O_RDONLY) {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
        iunlockput(ip);
        end_op();
        return -1;
    }

    if ((f = filealloc()) == nullptr || (fd = fdalloc(f)) < 0) {
        if (f) {
            fileclose(f);
        }
        iunlockput(ip);
        end_op();
        return -1;
    }

    if (ip->type == T_DEVICE) {
        f->type = fd_device;
        f->major = ip->major;
    } else {
        f->type = fd_inode;
        f->off = 0;
    }
    f->ip = ip;
    f->readable = !(omode & O_WRONLY);
    f->writable = omode & O_WRONLY || omode & O_RDWR;

    if (omode & O_TRUNC && ip->type == T_FILE) {
        itrunc(ip);
    }

    iunlock(ip);
    end_op();

    return fd;
}

uint64 sys_mkdir() {
    std::array<char, MAXPATH> path{};
    inode *ip;

    begin_op();
    if (argstr(0, path.data(), MAXPATH) < 0 ||
        (ip = create(path.data(), T_DIR, 0, 0)) == nullptr) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

uint64 sys_mknod() {
    inode *ip;
    std::array<char, MAXPATH> path{};
    int major, minor;

    begin_op();
    argint(1, &major);
    argint(2, &minor);
    if (argstr(0, path.data(), MAXPATH) < 0 ||
        (ip = create(path.data(), T_DEVICE, static_cast<short>(major),
                     static_cast<short>(minor))) == nullptr) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

uint64 sys_chdir() {
    std::array<char, MAXPATH> path{};
    inode *ip;
    proc *p = myproc();

    begin_op();
    if (argstr(0, path.data(), MAXPATH) < 0 ||
        (ip = namei(path.data())) == nullptr) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    iput(p->cwd);
    end_op();
    p->cwd = ip;
    return 0;
}

uint64 sys_exec() {
    std::array<char, MAXPATH> path{};
    std::array<const char *, MAXARG> argv{};
    uint i;
    int ret;
    uint64 uargv, uarg;

    argaddr(1, &uargv);
    if (argstr(0, path.data(), MAXPATH) < 0) {
        return -1;
    }
    memset(argv.data(), 0, sizeof(argv));
    for (i = 0;; i++) {
        if (i >= argv.size()) {
            goto bad;
        }
        if (fetchaddr(uargv + sizeof(uint64) * i, &uarg) < 0) {
            goto bad;
        }
        if (uarg == 0) {
            argv[i] = nullptr;
            break;
        }
        argv[i] = static_cast<char *>(page_allocator::instance().alloc());
        if (argv[i] == nullptr) {
            goto bad;
        }
        if (fetchstr(uarg, (char *)argv[i], PGSIZE) < 0) {
            goto bad;
        }
    }

    ret = kexec(path.data(), argv.data());

    for (i = 0; i < NELEM(argv) && argv[i] != nullptr; i++) {
        page_allocator::instance().free((void *)argv[i]);
    }

    return ret;

bad:
    for (i = 0; i < NELEM(argv) && argv[i] != nullptr; i++) {
        page_allocator::instance().free((void *)argv[i]);
    }
    return -1;
}

uint64 sys_pipe() {
    uint64 fdarray; // user pointer to array of two integers
    file *rf, *wf;
    int fd1;
    proc *p = myproc();

    argaddr(0, &fdarray);
    if (pipealloc(&rf, &wf) < 0) {
        return -1;
    }
    int fd0 = -1;
    if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
        if (fd0 >= 0) {
            p->ofile[fd0] = nullptr;
        }
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    if (copyout(p->pt, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
        copyout(p->pt, fdarray + sizeof(fd0), (char *)&fd1,
                sizeof(fd1)) < 0) {
        p->ofile[fd0] = nullptr;
        p->ofile[fd1] = nullptr;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    return 0;
}

} // namespace xv6