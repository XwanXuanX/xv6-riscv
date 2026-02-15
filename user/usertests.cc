#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

//
// Tests xv6 system calls.  usertests without arguments runs them all
// and usertests <name> runs <name> test. The test runner creates for
// each test a process and based on the exit status of the process,
// the test runner reports "OK" or "FAILED".  Some tests result in
// kernel printing usertrap messages, which can be ignored if test
// prints "OK".
//

#define BUFSZ ((MAXOPBLOCKS + 2) * BSIZE)

char buf[BUFSZ];

//
// Section with tests that run fairly quickly.  Use -q if you want to
// run just those.  Without -q usertests also runs the ones that take a
// fair amount of time.
//

// what if you pass ridiculous pointers to system calls
// that read user memory with copyin?
void copyin(const char *s) {
    uint64 addrs[] = {0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                      0xffffffffffffffff};

    for (const unsigned long addr : addrs) {
        const int fd = open("copyin1", O_CREATE | O_WRONLY);
        if (fd < 0) {
            printf("open(copyin1) failed\n");
            exit(1);
        }
        int n = write(fd, reinterpret_cast<void *>(addr), 8192);
        if (n >= 0) {
            printf("write(fd, %p, 8192) returned %d, not -1\n", (void *)addr, n);
            exit(1);
        }
        close(fd);
        unlink("copyin1");

        n = write(1, reinterpret_cast<char *>(addr), 8192);
        if (n > 0) {
            printf("write(1, %p, 8192) returned %d, not -1 or 0\n", (void *)addr, n);
            exit(1);
        }

        int fds[2];
        if (pipe(fds) < 0) {
            printf("pipe() failed\n");
            exit(1);
        }
        n = write(fds[1], (char *)addr, 8192);
        if (n > 0) {
            printf("write(pipe, %p, 8192) returned %d, not -1 or 0\n", (void *)addr, n);
            exit(1);
        }
        close(fds[0]);
        close(fds[1]);
    }
}

// what if you pass ridiculous pointers to system calls
// that write user memory with copyout?
void copyout(const char *s) {
    constexpr uint64 addrs[] = {0LL, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                                0xffffffffffffffff};

    for (unsigned long addr : addrs) {
        const int fd = open("README", 0);
        if (fd < 0) {
            printf("open(README) failed\n");
            exit(1);
        }
        int n = read(fd, (void *)addr, 8192);
        if (n > 0) {
            printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", (void *)addr, n);
            exit(1);
        }
        close(fd);

        int fds[2];
        if (pipe(fds) < 0) {
            printf("pipe() failed\n");
            exit(1);
        }
        n = write(fds[1], "x", 1);
        if (n != 1) {
            printf("pipe write failed\n");
            exit(1);
        }
        n = read(fds[0], (void *)addr, 8192);
        if (n > 0) {
            printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", (void *)addr, n);
            exit(1);
        }
        close(fds[0]);
        close(fds[1]);
    }
}

// what if you pass ridiculous string pointers to system calls?
void copyinstr1(const char *s) {
    constexpr uint64 addrs[] = {0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                                0xffffffffffffffff};

    for (unsigned long addr : addrs) {
        const int fd = open((char *)addr, O_CREATE | O_WRONLY);
        if (fd >= 0) {
            printf("open(%p) returned %d, not -1\n", (void *)addr, fd);
            exit(1);
        }
    }
}

// what if a string system call argument is exactly the size
// of the kernel buffer it is copied into, so that the null
// would fall just beyond the end of the kernel buffer?
void copyinstr2(const char *s) {
    char b[MAXPATH + 1];

    for (int i = 0; i < MAXPATH; i++) {
        b[i] = 'x';
    }
    b[MAXPATH] = '\0';

    int ret = unlink(b);
    if (ret != -1) {
        printf("unlink(%s) returned %d, not -1\n", b, ret);
        exit(1);
    }

    const int fd = open(b, O_CREATE | O_WRONLY);
    if (fd != -1) {
        printf("open(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }

    ret = link(b, b);
    if (ret != -1) {
        printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
        exit(1);
    }

    const char *args[] = {"xx", nullptr};
    ret = exec(b, args);
    if (ret != -1) {
        printf("exec(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }

    const int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        static char big[PGSIZE + 1];
        for (int i = 0; i < PGSIZE; i++) {
            big[i] = 'x';
        }
        big[PGSIZE] = '\0';
        const char *args2[] = {big, big, big, nullptr};
        ret = exec("echo", args2);
        if (ret != -1) {
            printf("exec(echo, BIG) returned %d, not -1\n", fd);
            exit(1);
        }
        exit(747); // OK
    }

    int st = 0;
    wait(&st);
    if (st != 747) {
        printf("exec(echo, BIG) succeeded, should have failed\n");
        exit(1);
    }
}

// what if a string argument crosses over the end of last user page?
void copyinstr3(const char *s) {
    sbrk(8192);
    auto top = reinterpret_cast<uint64>(sbrk(0));
    if (top % PGSIZE != 0) {
        sbrk(PGSIZE - top % PGSIZE);
    }
    top = (uint64)sbrk(0);
    if (top % PGSIZE) {
        printf("oops\n");
        exit(1);
    }

    const auto b = (char *)(top - 1);
    *b = 'x';

    int ret = unlink(b);
    if (ret != -1) {
        printf("unlink(%s) returned %d, not -1\n", b, ret);
        exit(1);
    }

    const int fd = open(b, O_CREATE | O_WRONLY);
    if (fd != -1) {
        printf("open(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }

    ret = link(b, b);
    if (ret != -1) {
        printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
        exit(1);
    }

    const char *args[] = {"xx", nullptr};
    ret = exec(b, args);
    if (ret != -1) {
        printf("exec(%s) returned %d, not -1\n", b, fd);
        exit(1);
    }
}

// See if the kernel refuses to read/write user memory that the
// application doesn't have anymore, because it returned it.
void rwsbrk(const char *s) {
    const auto a = reinterpret_cast<uint64>(sbrk(8192));

    if (a == reinterpret_cast<uint64>(SBRK_ERROR)) {
        printf("sbrk(rwsbrk) failed\n");
        exit(1);
    }

    if (sbrk(-8192) == SBRK_ERROR) {
        printf("sbrk(rwsbrk) shrink failed\n");
        exit(1);
    }

    int fd = open("rwsbrk", O_CREATE | O_WRONLY);
    if (fd < 0) {
        printf("open(rwsbrk) failed\n");
        exit(1);
    }
    int n = write(fd, (void *)(a + PGSIZE), 1024);
    if (n >= 0) {
        printf("write(fd, %p, 1024) returned %d, not -1\n", (char *)a + PGSIZE, n);
        exit(1);
    }
    close(fd);
    unlink("rwsbrk");

    fd = open("README", O_RDONLY);
    if (fd < 0) {
        printf("open(README) failed\n");
        exit(1);
    }
    n = read(fd, (void *)(a + PGSIZE), 10);
    if (n >= 0) {
        printf("read(fd, %p, 10) returned %d, not -1\n", (char *)a + PGSIZE, n);
        exit(1);
    }
    close(fd);

    exit(0);
}

// test O_TRUNC.
void truncate1(const char *s) {
    char buf[32];

    unlink("truncfile");
    int fd1 = open("truncfile", O_CREATE | O_WRONLY | O_TRUNC);
    write(fd1, "abcd", 4);
    close(fd1);

    const int fd2 = open("truncfile", O_RDONLY);
    int n = read(fd2, buf, sizeof(buf));
    if (n != 4) {
        printf("%s: read %d bytes, wanted 4\n", s, n);
        exit(1);
    }

    fd1 = open("truncfile", O_WRONLY | O_TRUNC);

    const int fd3 = open("truncfile", O_RDONLY);
    n = read(fd3, buf, sizeof(buf));
    if (n != 0) {
        printf("aaa fd3=%d\n", fd3);
        printf("%s: read %d bytes, wanted 0\n", s, n);
        exit(1);
    }

    n = read(fd2, buf, sizeof(buf));
    if (n != 0) {
        printf("bbb fd2=%d\n", fd2);
        printf("%s: read %d bytes, wanted 0\n", s, n);
        exit(1);
    }

    write(fd1, "abcdef", 6);

    n = read(fd3, buf, sizeof(buf));
    if (n != 6) {
        printf("%s: read %d bytes, wanted 6\n", s, n);
        exit(1);
    }

    n = read(fd2, buf, sizeof(buf));
    if (n != 2) {
        printf("%s: read %d bytes, wanted 2\n", s, n);
        exit(1);
    }

    unlink("truncfile");

    close(fd1);
    close(fd2);
    close(fd3);
}

// write to an open FD whose file has just been truncated.
// this causes a write at an offset beyond the end of the file.
// such writes fail on xv6 (unlike POSIX) but at least
// they don't crash.
void truncate2(const char *s) {
    unlink("truncfile");

    const int fd1 = open("truncfile", O_CREATE | O_TRUNC | O_WRONLY);
    write(fd1, "abcd", 4);

    const int fd2 = open("truncfile", O_TRUNC | O_WRONLY);

    const int n = write(fd1, "x", 1);
    if (n != -1) {
        printf("%s: write returned %d, expected -1\n", s, n);
        exit(1);
    }

    unlink("truncfile");
    close(fd1);
    close(fd2);
}

void truncate3(const char *s) {
    int xstatus;

    close(open("truncfile", O_CREATE | O_TRUNC | O_WRONLY));

    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }

    if (pid == 0) {
        for (int i = 0; i < 100; i++) {
            char buf[32];
            int fd = open("truncfile", O_WRONLY);
            if (fd < 0) {
                printf("%s: open failed\n", s);
                exit(1);
            }
            const int n = write(fd, "1234567890", 10);
            if (n != 10) {
                printf("%s: write got %d, expected 10\n", s, n);
                exit(1);
            }
            close(fd);
            fd = open("truncfile", O_RDONLY);
            read(fd, buf, sizeof(buf));
            close(fd);
        }
        exit(0);
    }

    for (int i = 0; i < 150; i++) {
        const int fd = open("truncfile", O_CREATE | O_WRONLY | O_TRUNC);
        if (fd < 0) {
            printf("%s: open failed\n", s);
            exit(1);
        }
        const int n = write(fd, "xxx", 3);
        if (n != 3) {
            printf("%s: write got %d, expected 3\n", s, n);
            exit(1);
        }
        close(fd);
    }

    wait(&xstatus);
    unlink("truncfile");
    exit(xstatus);
}

// does chdir() call iput(p->cwd) in a transaction?
void iputtest(const char *s) {
    if (mkdir("iputdir") < 0) {
        printf("%s: mkdir failed\n", s);
        exit(1);
    }
    if (chdir("iputdir") < 0) {
        printf("%s: chdir iputdir failed\n", s);
        exit(1);
    }
    if (unlink("../iputdir") < 0) {
        printf("%s: unlink ../iputdir failed\n", s);
        exit(1);
    }
    if (chdir("/") < 0) {
        printf("%s: chdir / failed\n", s);
        exit(1);
    }
}

// does exit() call iput(p->cwd) in a transaction?
void exitiputtest(const char *s) {
    int xstatus;

    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid == 0) {
        if (mkdir("iputdir") < 0) {
            printf("%s: mkdir failed\n", s);
            exit(1);
        }
        if (chdir("iputdir") < 0) {
            printf("%s: child chdir failed\n", s);
            exit(1);
        }
        if (unlink("../iputdir") < 0) {
            printf("%s: unlink ../iputdir failed\n", s);
            exit(1);
        }
        exit(0);
    }
    wait(&xstatus);
    exit(xstatus);
}

// does the error path in open() for attempt to write a
// directory call iput() in a transaction?
// needs a hacked kernel that pauses just after the namei()
// call in sys_open():
//    if((ip = namei(path)) == 0)
//      return -1;
//    {
//      int i;
//      for(i = 0; i < 10000; i++)
//        yield();
//    }
void openiputtest(const char *s) {
    int xstatus;

    if (mkdir("oidir") < 0) {
        printf("%s: mkdir oidir failed\n", s);
        exit(1);
    }
    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid == 0) {
        const int fd = open("oidir", O_RDWR);
        if (fd >= 0) {
            printf("%s: open directory for write succeeded\n", s);
            exit(1);
        }
        exit(0);
    }
    pause(1);
    if (unlink("oidir") != 0) {
        printf("%s: unlink failed\n", s);
        exit(1);
    }
    wait(&xstatus);
    exit(xstatus);
}

// simple file system tests

void opentest(const char *s) {
    int fd = open("echo", 0);
    if (fd < 0) {
        printf("%s: open echo failed!\n", s);
        exit(1);
    }
    close(fd);
    fd = open("doesnotexist", 0);
    if (fd >= 0) {
        printf("%s: open doesnotexist succeeded!\n", s);
        exit(1);
    }
}

void writetest(const char *s) {
    int i;
    enum { N = 100,
           SZ = 10 };

    int fd = open("small", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: error: creat small failed!\n", s);
        exit(1);
    }
    for (i = 0; i < N; i++) {
        if (write(fd, "aaaaaaaaaa", SZ) != SZ) {
            printf("%s: error: write aa %d new file failed\n", s, i);
            exit(1);
        }
        if (write(fd, "bbbbbbbbbb", SZ) != SZ) {
            printf("%s: error: write bb %d new file failed\n", s, i);
            exit(1);
        }
    }
    close(fd);
    fd = open("small", O_RDONLY);
    if (fd < 0) {
        printf("%s: error: open small failed!\n", s);
        exit(1);
    }
    i = read(fd, buf, N * SZ * 2);
    if (i != N * SZ * 2) {
        printf("%s: read failed\n", s);
        exit(1);
    }
    close(fd);

    if (unlink("small") < 0) {
        printf("%s: unlink small failed\n", s);
        exit(1);
    }
}

void writebig(const char *s) {
    int i;

    int fd = open("big", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: error: creat big failed!\n", s);
        exit(1);
    }

    for (i = 0; i < static_cast<int>(MAXFILE); i++) {
        ((int *)buf)[0] = i;
        if (write(fd, buf, BSIZE) != BSIZE) {
            printf("%s: error: write big file failed i=%d\n", s, i);
            exit(1);
        }
    }

    close(fd);

    fd = open("big", O_RDONLY);
    if (fd < 0) {
        printf("%s: error: open big failed!\n", s);
        exit(1);
    }

    int n = 0;
    for (;;) {
        i = read(fd, buf, BSIZE);
        if (i == 0) {
            if (n != MAXFILE) {
                printf("%s: read only %d blocks from big", s, n);
                exit(1);
            }
            break;
        }
        if (i != BSIZE) {
            printf("%s: read failed %d\n", s, i);
            exit(1);
        }
        if (((int *)buf)[0] != n) {
            printf("%s: read content of block %d is %d\n", s,
                   n, ((int *)buf)[0]);
            exit(1);
        }
        n++;
    }
    close(fd);
    if (unlink("big") < 0) {
        printf("%s: unlink big failed\n", s);
        exit(1);
    }
}

// many creates, followed by unlink test
void createtest(const char *s) {
    int i;
    enum { N = 52 };

    char name[3];
    name[0] = 'a';
    name[2] = '\0';
    for (i = 0; i < N; i++) {
        name[1] = '0' + i;
        const int fd = open(name, O_CREATE | O_RDWR);
        close(fd);
    }
    name[0] = 'a';
    name[2] = '\0';
    for (i = 0; i < N; i++) {
        name[1] = '0' + i;
        unlink(name);
    }
}

void dirtest(const char *s) {
    if (mkdir("dir0") < 0) {
        printf("%s: mkdir failed\n", s);
        exit(1);
    }

    if (chdir("dir0") < 0) {
        printf("%s: chdir dir0 failed\n", s);
        exit(1);
    }

    if (chdir("..") < 0) {
        printf("%s: chdir .. failed\n", s);
        exit(1);
    }

    if (unlink("dir0") < 0) {
        printf("%s: unlink dir0 failed\n", s);
        exit(1);
    }
}

void exectest(const char *s) {
    int fd, xstatus;
    const char *echoargv[] = {"echo", "OK", nullptr};
    char buf[3];

    unlink("echo-ok");
    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid == 0) {
        close(1);
        fd = open("echo-ok", O_CREATE | O_WRONLY);
        if (fd < 0) {
            printf("%s: create failed\n", s);
            exit(1);
        }
        if (fd != 1) {
            printf("%s: wrong fd\n", s);
            exit(1);
        }
        if (exec("echo", echoargv) < 0) {
            printf("%s: exec echo failed\n", s);
            exit(1);
        }
        // won't get to here
    }
    if (wait(&xstatus) != pid) {
        printf("%s: wait failed!\n", s);
    }
    if (xstatus != 0) {
        exit(xstatus);
    }

    fd = open("echo-ok", O_RDONLY);
    if (fd < 0) {
        printf("%s: open failed\n", s);
        exit(1);
    }
    if (read(fd, buf, 2) != 2) {
        printf("%s: read failed\n", s);
        exit(1);
    }
    unlink("echo-ok");
    if (buf[0] == 'O' && buf[1] == 'K') {
        exit(0);
    }
    printf("%s: wrong output\n", s);
    exit(1);
}

// simple fork and pipe read/write

void pipe1(const char *s) {
    int fds[2], xstatus;
    int i, n;
    enum { N = 5,
           SZ = 1033 };

    if (pipe(fds) != 0) {
        printf("%s: pipe() failed\n", s);
        exit(1);
    }
    const int pid = fork();
    int seq = 0;
    if (pid == 0) {
        close(fds[0]);
        for (n = 0; n < N; n++) {
            for (i = 0; i < SZ; i++) {
                buf[i] = seq++;
            }
            if (write(fds[1], buf, SZ) != SZ) {
                printf("%s: pipe1 oops 1\n", s);
                exit(1);
            }
        }
        exit(0);
    }
    if (pid > 0) {
        close(fds[1]);
        int total = 0;
        int cc = 1;
        while ((n = read(fds[0], buf, cc)) > 0) {
            for (i = 0; i < n; i++) {
                if ((buf[i] & 0xff) != (seq++ & 0xff)) {
                    printf("%s: pipe1 oops 2\n", s);
                    return;
                }
            }
            total += n;
            cc = cc * 2;
            if (cc > static_cast<int>(sizeof(buf))) {
                cc = sizeof(buf);
            }
        }
        if (total != N * SZ) {
            printf("%s: pipe1 oops 3 total %d\n", s, total);
            exit(1);
        }
        close(fds[0]);
        wait(&xstatus);
        exit(xstatus);
    }
    printf("%s: fork() failed\n", s);
    exit(1);
}

// test if child is killed (status = -1)
void killstatus(const char *s) {
    int xst;

    for (int i = 0; i < 100; i++) {
        const int pid1 = fork();
        if (pid1 < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid1 == 0) {
            while (true) {
                getpid();
            }
            exit(0);
        }
        pause(1);
        kill(pid1);
        wait(&xst);
        if (xst != -1) {
            printf("%s: status should be -1\n", s);
            exit(1);
        }
    }
    exit(0);
}

// meant to be run w/ at most two CPUs
void preempt(const char *s) {
    int pfds[2];

    const int pid1 = fork();
    if (pid1 < 0) {
        printf("%s: fork failed", s);
        exit(1);
    }
    if (pid1 == 0) {
        for (;;)
            ;
    }

    const int pid2 = fork();
    if (pid2 < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid2 == 0) {
        for (;;)
            ;
    }

    pipe(pfds);
    const int pid3 = fork();
    if (pid3 < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid3 == 0) {
        close(pfds[0]);
        if (write(pfds[1], "x", 1) != 1) {
            printf("%s: preempt write error", s);
        }
        close(pfds[1]);
        for (;;)
            ;
    }

    close(pfds[1]);
    if (read(pfds[0], buf, sizeof(buf)) != 1) {
        printf("%s: preempt read error", s);
        return;
    }
    close(pfds[0]);
    printf("kill... ");
    kill(pid1);
    kill(pid2);
    kill(pid3);
    printf("wait... ");
    wait(nullptr);
    wait(nullptr);
    wait(nullptr);
}

// try to find any races between exit and wait
void exitwait(const char *s) {
    for (int i = 0; i < 100; i++) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid) {
            int xstate;
            if (wait(&xstate) != pid) {
                printf("%s: wait wrong pid\n", s);
                exit(1);
            }
            if (i != xstate) {
                printf("%s: wait wrong exit status\n", s);
                exit(1);
            }
        } else {
            exit(i);
        }
    }
}

// try to find races in the reparenting
// code that handles a parent exiting
// when it still has live children.
void reparent(const char *s) {
    const int master_pid = getpid();
    for (int i = 0; i < 200; i++) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid) {
            if (wait(nullptr) != pid) {
                printf("%s: wait wrong pid\n", s);
                exit(1);
            }
        } else {
            const int pid2 = fork();
            if (pid2 < 0) {
                kill(master_pid);
                exit(1);
            }
            exit(0);
        }
    }
    exit(0);
}

// what if two children exit() at the same time?
void twochildren(const char *s) {
    for (int i = 0; i < 1000; i++) {
        const int pid1 = fork();
        if (pid1 < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid1 == 0) {
            exit(0);
        }
        const int pid2 = fork();
        if (pid2 < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid2 == 0) {
            exit(0);
        }
        wait(nullptr);
        wait(nullptr);
    }
}

// concurrent forks to try to expose locking bugs.
void forkfork(const char *s) {
    enum { N = 2 };

    for (int i = 0; i < N; i++) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed", s);
            exit(1);
        }
        if (pid == 0) {
            for (int j = 0; j < 200; j++) {
                const int pid1 = fork();
                if (pid1 < 0) {
                    exit(1);
                }
                if (pid1 == 0) {
                    exit(0);
                }
                wait(nullptr);
            }
            exit(0);
        }
    }

    int xstatus;
    for (int i = 0; i < N; i++) {
        wait(&xstatus);
        if (xstatus != 0) {
            printf("%s: fork in child failed", s);
            exit(1);
        }
    }
}

void forkforkfork(const char *s) {
    unlink("stopforking");

    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed", s);
        exit(1);
    }
    if (pid == 0) {
        while (true) {
            const int fd = open("stopforking", 0);
            if (fd >= 0) {
                exit(0);
            }
            if (fork() < 0) {
                close(open("stopforking", O_CREATE | O_RDWR));
            }
        }

        exit(0);
    }

    pause(20); // two seconds
    close(open("stopforking", O_CREATE | O_RDWR));
    wait(nullptr);
    pause(10); // one second
}

// regression test. does reparent() violate the parent-then-child
// locking order when giving away a child to init, so that exit()
// deadlocks against init's wait()? also used to trigger a "panic:
// release" due to exit() releasing a different p->parent->lock than
// it acquired.
void reparent2(const char *s) {
    for (int i = 0; i < 800; i++) {
        const int pid1 = fork();
        if (pid1 < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid1 == 0) {
            fork();
            fork();
            exit(0);
        }
        wait(nullptr);
    }

    exit(0);
}

// allocate all mem, free it, and allocate again
void mem(const char *s) {
    if (fork() == 0) {
        void *m2;
        void *m1 = nullptr;
        while ((m2 = malloc(10001)) != nullptr) {
            *static_cast<char **>(m2) = static_cast<char *>(m1);
            m1 = m2;
        }
        while (m1) {
            m2 = *static_cast<char **>(m1);
            free(m1);
            m1 = m2;
        }
        m1 = malloc(1024 * 20);
        if (m1 == nullptr) {
            printf("%s: couldn't allocate mem?!!\n", s);
            exit(1);
        }
        free(m1);
        exit(0);
    }
    int xstatus;
    wait(&xstatus);
    if (xstatus == -1) {
        // probably page fault, so might be lazy lab,
        // so OK.
        exit(0);
    }
    exit(xstatus);
}

// More file system tests

// two processes write to the same file descriptor
// is the offset shared? does inode locking work?
void sharedfd(const char *s) {
    int i, np;
    enum { N = 1000,
           SZ = 10 };
    char buf[SZ];

    unlink("sharedfd");
    int fd = open("sharedfd", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: cannot open sharedfd for writing", s);
        exit(1);
    }
    const int pid = fork();
    memset(buf, pid == 0 ? 'c' : 'p', sizeof(buf));
    for (i = 0; i < N; i++) {
        if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
            printf("%s: write sharedfd failed\n", s);
            exit(1);
        }
    }
    if (pid == 0) {
        exit(0);
    }
    int xstatus;
    wait(&xstatus);
    if (xstatus != 0) {
        exit(xstatus);
    }

    close(fd);
    fd = open("sharedfd", 0);
    if (fd < 0) {
        printf("%s: cannot open sharedfd for reading\n", s);
        exit(1);
    }
    int nc = np = 0;
    while (read(fd, buf, sizeof(buf)) > 0) {
        for (i = 0; i < static_cast<int>(sizeof(buf)); i++) {
            if (buf[i] == 'c') {
                nc++;
            }
            if (buf[i] == 'p') {
                np++;
            }
        }
    }
    close(fd);
    unlink("sharedfd");
    if (nc == N * SZ && np == N * SZ) {
        exit(0);
    }
    printf("%s: nc/np test fails\n", s);
    exit(1);
}

// four processes write different files at the same
// time, to test block allocation.
void fourfiles(const char *s) {
    int fd, i, n, pi;
    const char *names[] = {"f0", "f1", "f2", "f3"};
    const char *fname;
    enum { N = 12,
           NCHILD = 4,
           SZ = 500 };

    for (pi = 0; pi < NCHILD; pi++) {
        fname = names[pi];
        unlink(fname);

        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }

        if (pid == 0) {
            fd = open(fname, O_CREATE | O_RDWR);
            if (fd < 0) {
                printf("%s: create failed\n", s);
                exit(1);
            }

            memset(buf, '0' + pi, SZ);
            for (i = 0; i < N; i++) {
                if ((n = write(fd, buf, SZ)) != SZ) {
                    printf("write failed %d\n", n);
                    exit(1);
                }
            }
            exit(0);
        }
    }

    int xstatus;
    for (pi = 0; pi < NCHILD; pi++) {
        wait(&xstatus);
        if (xstatus != 0) {
            exit(xstatus);
        }
    }

    for (i = 0; i < NCHILD; i++) {
        fname = names[i];
        fd = open(fname, 0);
        int total = 0;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            for (int j = 0; j < n; j++) {
                if (buf[j] != '0' + i) {
                    printf("%s: wrong char\n", s);
                    exit(1);
                }
            }
            total += n;
        }
        close(fd);
        if (total != N * SZ) {
            printf("wrong length %d\n", total);
            exit(1);
        }
        unlink(fname);
    }
}

// four processes create and delete different files in same directory
void createdelete(const char *s) {
    enum { N = 20,
           NCHILD = 4 };
    int i, fd, pi;
    char name[32];

    for (pi = 0; pi < NCHILD; pi++) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }

        if (pid == 0) {
            name[0] = 'p' + pi;
            name[2] = '\0';
            for (i = 0; i < N; i++) {
                name[1] = '0' + i;
                fd = open(name, O_CREATE | O_RDWR);
                if (fd < 0) {
                    printf("%s: create failed\n", s);
                    exit(1);
                }
                close(fd);
                if (i > 0 && i % 2 == 0) {
                    name[1] = '0' + i / 2;
                    if (unlink(name) < 0) {
                        printf("%s: unlink failed\n", s);
                        exit(1);
                    }
                }
            }
            exit(0);
        }
    }

    int xstatus;
    for (pi = 0; pi < NCHILD; pi++) {
        wait(&xstatus);
        if (xstatus != 0) {
            exit(1);
        }
    }

    name[0] = name[1] = name[2] = 0;
    for (i = 0; i < N; i++) {
        for (pi = 0; pi < NCHILD; pi++) {
            name[0] = 'p' + pi;
            name[1] = '0' + i;
            fd = open(name, 0);
            if ((i == 0 || i >= N / 2) && fd < 0) {
                printf("%s: oops createdelete %s didn't exist\n", s, name);
                exit(1);
            }
            if (i >= 1 && i < N / 2 && fd >= 0) {
                printf("%s: oops createdelete %s did exist\n", s, name);
                exit(1);
            }
            if (fd >= 0) {
                close(fd);
            }
        }
    }

    for (i = 0; i < N; i++) {
        for (pi = 0; pi < NCHILD; pi++) {
            name[0] = 'p' + pi;
            name[1] = '0' + i;
            unlink(name);
        }
    }
}

// can I unlink a file and still read it?
void unlinkread(const char *s) {
    enum { SZ = 5 };

    int fd = open("unlinkread", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: create unlinkread failed\n", s);
        exit(1);
    }
    write(fd, "hello", SZ);
    close(fd);

    fd = open("unlinkread", O_RDWR);
    if (fd < 0) {
        printf("%s: open unlinkread failed\n", s);
        exit(1);
    }
    if (unlink("unlinkread") != 0) {
        printf("%s: unlink unlinkread failed\n", s);
        exit(1);
    }

    const int fd1 = open("unlinkread", O_CREATE | O_RDWR);
    write(fd1, "yyy", 3);
    close(fd1);

    if (read(fd, buf, sizeof(buf)) != SZ) {
        printf("%s: unlinkread read failed", s);
        exit(1);
    }
    if (buf[0] != 'h') {
        printf("%s: unlinkread wrong data\n", s);
        exit(1);
    }
    if (write(fd, buf, 10) != 10) {
        printf("%s: unlinkread write failed\n", s);
        exit(1);
    }
    close(fd);
    unlink("unlinkread");
}

void linktest(const char *s) {
    enum { SZ = 5 };

    unlink("lf1");
    unlink("lf2");

    int fd = open("lf1", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: create lf1 failed\n", s);
        exit(1);
    }
    if (write(fd, "hello", SZ) != SZ) {
        printf("%s: write lf1 failed\n", s);
        exit(1);
    }
    close(fd);

    if (link("lf1", "lf2") < 0) {
        printf("%s: link lf1 lf2 failed\n", s);
        exit(1);
    }
    unlink("lf1");

    if (open("lf1", 0) >= 0) {
        printf("%s: unlinked lf1 but it is still there!\n", s);
        exit(1);
    }

    fd = open("lf2", 0);
    if (fd < 0) {
        printf("%s: open lf2 failed\n", s);
        exit(1);
    }
    if (read(fd, buf, sizeof(buf)) != SZ) {
        printf("%s: read lf2 failed\n", s);
        exit(1);
    }
    close(fd);

    if (link("lf2", "lf2") >= 0) {
        printf("%s: link lf2 lf2 succeeded! oops\n", s);
        exit(1);
    }

    unlink("lf2");
    if (link("lf2", "lf1") >= 0) {
        printf("%s: link non-existent succeeded! oops\n", s);
        exit(1);
    }

    if (link(".", "lf1") >= 0) {
        printf("%s: link . lf1 succeeded! oops\n", s);
        exit(1);
    }
}

// test concurrent create/link/unlink of the same file
void concreate(const char *s) {
    enum { N = 40 };
    char file[3];
    int i, pid, fd;
    char fa[N];
    struct {
        ushort inum;
        char name[DIRSIZ];
    } de;

    file[0] = 'C';
    file[2] = '\0';
    for (i = 0; i < N; i++) {
        file[1] = '0' + i;
        unlink(file);
        pid = fork();
        if (pid && i % 3 == 1) {
            link("C0", file);
        } else if (pid == 0 && i % 5 == 1) {
            link("C0", file);
        } else {
            fd = open(file, O_CREATE | O_RDWR);
            if (fd < 0) {
                printf("concreate create %s failed\n", file);
                exit(1);
            }
            close(fd);
        }
        if (pid == 0) {
            exit(0);
        }
        int xstatus;
        wait(&xstatus);
        if (xstatus != 0) {
            exit(1);
        }
    }

    memset(fa, 0, sizeof(fa));
    fd = open(".", 0);
    int n = 0;
    while (read(fd, &de, sizeof(de)) > 0) {
        if (de.inum == 0) {
            continue;
        }
        if (de.name[0] == 'C' && de.name[2] == '\0') {
            i = de.name[1] - '0';
            if (i < 0 || i >= static_cast<int>(sizeof(fa))) {
                printf("%s: concreate weird file %s\n", s, de.name);
                exit(1);
            }
            if (fa[i]) {
                printf("%s: concreate duplicate file %s\n", s, de.name);
                exit(1);
            }
            fa[i] = 1;
            n++;
        }
    }
    close(fd);

    if (n != N) {
        printf("%s: concreate not enough files in directory listing\n", s);
        exit(1);
    }

    for (i = 0; i < N; i++) {
        file[1] = '0' + i;
        pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if ((i % 3 == 0 && pid == 0) ||
            (i % 3 == 1 && pid != 0)) {
            close(open(file, 0));
            close(open(file, 0));
            close(open(file, 0));
            close(open(file, 0));
            close(open(file, 0));
            close(open(file, 0));
        } else {
            unlink(file);
            unlink(file);
            unlink(file);
            unlink(file);
            unlink(file);
            unlink(file);
        }
        if (pid == 0) {
            exit(0);
        }
        wait(nullptr);
    }
}

// another concurrent link/unlink/create test,
// to look for deadlocks.
void linkunlink(const char *s) {
    unlink("x");
    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }

    unsigned int x = pid ? 1 : 97;
    for (int i = 0; i < 100; i++) {
        x = x * 1103515245 + 12345;
        if (x % 3 == 0) {
            close(open("x", O_RDWR | O_CREATE));
        } else if (x % 3 == 1) {
            link("cat", "x");
        } else {
            unlink("x");
        }
    }

    if (pid) {
        wait(nullptr);
    } else {
        exit(0);
    }
}

void subdir(const char *s) {
    unlink("ff");
    if (mkdir("dd") != 0) {
        printf("%s: mkdir dd failed\n", s);
        exit(1);
    }

    int fd = open("dd/ff", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: create dd/ff failed\n", s);
        exit(1);
    }
    write(fd, "ff", 2);
    close(fd);

    if (unlink("dd") >= 0) {
        printf("%s: unlink dd (non-empty dir) succeeded!\n", s);
        exit(1);
    }

    if (mkdir("/dd/dd") != 0) {
        printf("%s: subdir mkdir dd/dd failed\n", s);
        exit(1);
    }

    fd = open("dd/dd/ff", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: create dd/dd/ff failed\n", s);
        exit(1);
    }
    write(fd, "FF", 2);
    close(fd);

    fd = open("dd/dd/../ff", 0);
    if (fd < 0) {
        printf("%s: open dd/dd/../ff failed\n", s);
        exit(1);
    }
    const int cc = read(fd, buf, sizeof(buf));
    if (cc != 2 || buf[0] != 'f') {
        printf("%s: dd/dd/../ff wrong content\n", s);
        exit(1);
    }
    close(fd);

    if (link("dd/dd/ff", "dd/dd/ffff") != 0) {
        printf("%s: link dd/dd/ff dd/dd/ffff failed\n", s);
        exit(1);
    }

    if (unlink("dd/dd/ff") != 0) {
        printf("%s: unlink dd/dd/ff failed\n", s);
        exit(1);
    }
    if (open("dd/dd/ff", O_RDONLY) >= 0) {
        printf("%s: open (unlinked) dd/dd/ff succeeded\n", s);
        exit(1);
    }

    if (chdir("dd") != 0) {
        printf("%s: chdir dd failed\n", s);
        exit(1);
    }
    if (chdir("dd/../../dd") != 0) {
        printf("%s: chdir dd/../../dd failed\n", s);
        exit(1);
    }
    if (chdir("dd/../../../dd") != 0) {
        printf("%s: chdir dd/../../../dd failed\n", s);
        exit(1);
    }
    if (chdir("./..") != 0) {
        printf("%s: chdir ./.. failed\n", s);
        exit(1);
    }

    fd = open("dd/dd/ffff", 0);
    if (fd < 0) {
        printf("%s: open dd/dd/ffff failed\n", s);
        exit(1);
    }
    if (read(fd, buf, sizeof(buf)) != 2) {
        printf("%s: read dd/dd/ffff wrong len\n", s);
        exit(1);
    }
    close(fd);

    if (open("dd/dd/ff", O_RDONLY) >= 0) {
        printf("%s: open (unlinked) dd/dd/ff succeeded!\n", s);
        exit(1);
    }

    if (open("dd/ff/ff", O_CREATE | O_RDWR) >= 0) {
        printf("%s: create dd/ff/ff succeeded!\n", s);
        exit(1);
    }
    if (open("dd/xx/ff", O_CREATE | O_RDWR) >= 0) {
        printf("%s: create dd/xx/ff succeeded!\n", s);
        exit(1);
    }
    if (open("dd", O_CREATE) >= 0) {
        printf("%s: create dd succeeded!\n", s);
        exit(1);
    }
    if (open("dd", O_RDWR) >= 0) {
        printf("%s: open dd rdwr succeeded!\n", s);
        exit(1);
    }
    if (open("dd", O_WRONLY) >= 0) {
        printf("%s: open dd wronly succeeded!\n", s);
        exit(1);
    }
    if (link("dd/ff/ff", "dd/dd/xx") == 0) {
        printf("%s: link dd/ff/ff dd/dd/xx succeeded!\n", s);
        exit(1);
    }
    if (link("dd/xx/ff", "dd/dd/xx") == 0) {
        printf("%s: link dd/xx/ff dd/dd/xx succeeded!\n", s);
        exit(1);
    }
    if (link("dd/ff", "dd/dd/ffff") == 0) {
        printf("%s: link dd/ff dd/dd/ffff succeeded!\n", s);
        exit(1);
    }
    if (mkdir("dd/ff/ff") == 0) {
        printf("%s: mkdir dd/ff/ff succeeded!\n", s);
        exit(1);
    }
    if (mkdir("dd/xx/ff") == 0) {
        printf("%s: mkdir dd/xx/ff succeeded!\n", s);
        exit(1);
    }
    if (mkdir("dd/dd/ffff") == 0) {
        printf("%s: mkdir dd/dd/ffff succeeded!\n", s);
        exit(1);
    }
    if (unlink("dd/xx/ff") == 0) {
        printf("%s: unlink dd/xx/ff succeeded!\n", s);
        exit(1);
    }
    if (unlink("dd/ff/ff") == 0) {
        printf("%s: unlink dd/ff/ff succeeded!\n", s);
        exit(1);
    }
    if (chdir("dd/ff") == 0) {
        printf("%s: chdir dd/ff succeeded!\n", s);
        exit(1);
    }
    if (chdir("dd/xx") == 0) {
        printf("%s: chdir dd/xx succeeded!\n", s);
        exit(1);
    }

    if (unlink("dd/dd/ffff") != 0) {
        printf("%s: unlink dd/dd/ff failed\n", s);
        exit(1);
    }
    if (unlink("dd/ff") != 0) {
        printf("%s: unlink dd/ff failed\n", s);
        exit(1);
    }
    if (unlink("dd") == 0) {
        printf("%s: unlink non-empty dd succeeded!\n", s);
        exit(1);
    }
    if (unlink("dd/dd") < 0) {
        printf("%s: unlink dd/dd failed\n", s);
        exit(1);
    }
    if (unlink("dd") < 0) {
        printf("%s: unlink dd failed\n", s);
        exit(1);
    }
}

// test writes that are larger than the log.
void bigwrite(const char *s) {
    unlink("bigwrite");
    for (int sz = 499; sz < (MAXOPBLOCKS + 2) * BSIZE; sz += 471) {
        const int fd = open("bigwrite", O_CREATE | O_RDWR);
        if (fd < 0) {
            printf("%s: cannot create bigwrite\n", s);
            exit(1);
        }
        for (int i = 0; i < 2; i++) {
            const int cc = write(fd, buf, sz);
            if (cc != sz) {
                printf("%s: write(%d) ret %d\n", s, sz, cc);
                exit(1);
            }
        }
        close(fd);
        unlink("bigwrite");
    }
}

void bigfile(const char *s) {
    enum { N = 20,
           SZ = 600 };
    int i;

    unlink("bigfile.dat");
    int fd = open("bigfile.dat", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("%s: cannot create bigfile", s);
        exit(1);
    }
    for (i = 0; i < N; i++) {
        memset(buf, i, SZ);
        if (write(fd, buf, SZ) != SZ) {
            printf("%s: write bigfile failed\n", s);
            exit(1);
        }
    }
    close(fd);

    fd = open("bigfile.dat", 0);
    if (fd < 0) {
        printf("%s: cannot open bigfile\n", s);
        exit(1);
    }
    int total = 0;
    for (i = 0;; i++) {
        const int cc = read(fd, buf, SZ / 2);
        if (cc < 0) {
            printf("%s: read bigfile failed\n", s);
            exit(1);
        }
        if (cc == 0) {
            break;
        }
        if (cc != SZ / 2) {
            printf("%s: short read bigfile\n", s);
            exit(1);
        }
        if (buf[0] != i / 2 || buf[SZ / 2 - 1] != i / 2) {
            printf("%s: read bigfile wrong data\n", s);
            exit(1);
        }
        total += cc;
    }
    close(fd);
    if (total != N * SZ) {
        printf("%s: read bigfile wrong total\n", s);
        exit(1);
    }
    unlink("bigfile.dat");
}

void fourteen(const char *s) {
    // DIRSIZ is 14.

    if (mkdir("12345678901234") != 0) {
        printf("%s: mkdir 12345678901234 failed\n", s);
        exit(1);
    }
    if (mkdir("12345678901234/123456789012345") != 0) {
        printf("%s: mkdir 12345678901234/123456789012345 failed\n", s);
        exit(1);
    }
    int fd = open("123456789012345/123456789012345/123456789012345", O_CREATE);
    if (fd < 0) {
        printf("%s: create 123456789012345/123456789012345/123456789012345 failed\n", s);
        exit(1);
    }
    close(fd);
    fd = open("12345678901234/12345678901234/12345678901234", 0);
    if (fd < 0) {
        printf("%s: open 12345678901234/12345678901234/12345678901234 failed\n", s);
        exit(1);
    }
    close(fd);

    if (mkdir("12345678901234/12345678901234") == 0) {
        printf("%s: mkdir 12345678901234/12345678901234 succeeded!\n", s);
        exit(1);
    }
    if (mkdir("123456789012345/12345678901234") == 0) {
        printf("%s: mkdir 12345678901234/123456789012345 succeeded!\n", s);
        exit(1);
    }

    // clean up
    unlink("123456789012345/12345678901234");
    unlink("12345678901234/12345678901234");
    unlink("12345678901234/12345678901234/12345678901234");
    unlink("123456789012345/123456789012345/123456789012345");
    unlink("12345678901234/123456789012345");
    unlink("12345678901234");
}

void rmdot(const char *s) {
    if (mkdir("dots") != 0) {
        printf("%s: mkdir dots failed\n", s);
        exit(1);
    }
    if (chdir("dots") != 0) {
        printf("%s: chdir dots failed\n", s);
        exit(1);
    }
    if (unlink(".") == 0) {
        printf("%s: rm . worked!\n", s);
        exit(1);
    }
    if (unlink("..") == 0) {
        printf("%s: rm .. worked!\n", s);
        exit(1);
    }
    if (chdir("/") != 0) {
        printf("%s: chdir / failed\n", s);
        exit(1);
    }
    if (unlink("dots/.") == 0) {
        printf("%s: unlink dots/. worked!\n", s);
        exit(1);
    }
    if (unlink("dots/..") == 0) {
        printf("%s: unlink dots/.. worked!\n", s);
        exit(1);
    }
    if (unlink("dots") != 0) {
        printf("%s: unlink dots failed!\n", s);
        exit(1);
    }
}

void dirfile(const char *s) {
    int fd = open("dirfile", O_CREATE);
    if (fd < 0) {
        printf("%s: create dirfile failed\n", s);
        exit(1);
    }
    close(fd);
    if (chdir("dirfile") == 0) {
        printf("%s: chdir dirfile succeeded!\n", s);
        exit(1);
    }
    fd = open("dirfile/xx", 0);
    if (fd >= 0) {
        printf("%s: create dirfile/xx succeeded!\n", s);
        exit(1);
    }
    fd = open("dirfile/xx", O_CREATE);
    if (fd >= 0) {
        printf("%s: create dirfile/xx succeeded!\n", s);
        exit(1);
    }
    if (mkdir("dirfile/xx") == 0) {
        printf("%s: mkdir dirfile/xx succeeded!\n", s);
        exit(1);
    }
    if (unlink("dirfile/xx") == 0) {
        printf("%s: unlink dirfile/xx succeeded!\n", s);
        exit(1);
    }
    if (link("README", "dirfile/xx") == 0) {
        printf("%s: link to dirfile/xx succeeded!\n", s);
        exit(1);
    }
    if (unlink("dirfile") != 0) {
        printf("%s: unlink dirfile failed!\n", s);
        exit(1);
    }

    fd = open(".", O_RDWR);
    if (fd >= 0) {
        printf("%s: open . for writing succeeded!\n", s);
        exit(1);
    }
    fd = open(".", 0);
    if (write(fd, "x", 1) > 0) {
        printf("%s: write . succeeded!\n", s);
        exit(1);
    }
    close(fd);
}

// test that iput() is called at the end of _namei().
// also tests empty file names.
void iref(const char *s) {
    int i;

    for (i = 0; i < NINODE + 1; i++) {
        if (mkdir("irefd") != 0) {
            printf("%s: mkdir irefd failed\n", s);
            exit(1);
        }
        if (chdir("irefd") != 0) {
            printf("%s: chdir irefd failed\n", s);
            exit(1);
        }

        mkdir("");
        link("README", "");
        int fd = open("", O_CREATE);
        if (fd >= 0) {
            close(fd);
        }
        fd = open("xx", O_CREATE);
        if (fd >= 0) {
            close(fd);
        }
        unlink("xx");
    }

    // clean up
    for (i = 0; i < NINODE + 1; i++) {
        chdir("..");
        unlink("irefd");
    }

    chdir("/");
}

// test that fork fails gracefully
// the forktest binary also does this, but it runs out of proc entries first.
// inside the bigger usertests binary, we run out of memory first.
void forktest(const char *s) {
    enum { N = 1000 };
    int n;

    for (n = 0; n < N; n++) {
        const int pid = fork();
        if (pid < 0) {
            break;
        }
        if (pid == 0) {
            exit(0);
        }
    }

    if (n == 0) {
        printf("%s: no fork at all!\n", s);
        exit(1);
    }

    if (n == N) {
        printf("%s: fork claimed to work 1000 times!\n", s);
        exit(1);
    }

    for (; n > 0; n--) {
        if (wait(nullptr) < 0) {
            printf("%s: wait stopped early\n", s);
            exit(1);
        }
    }

    if (wait(nullptr) != -1) {
        printf("%s: wait got too many\n", s);
        exit(1);
    }
}

void sbrkbasic(const char *s) {
    enum { TOOMUCH = 1024 * 1024 * 1024 };
    int xstatus;
    char *a, *b;

    // does sbrk() return the expected failure value?
    int pid = fork();
    if (pid < 0) {
        printf("fork failed in sbrkbasic\n");
        exit(1);
    }
    if (pid == 0) {
        a = sbrk(TOOMUCH);
        if (a == SBRK_ERROR) {
            // it's OK if this fails.
            exit(0);
        }

        for (b = a; b < a + TOOMUCH; b += PGSIZE) {
            *b = 99;
        }

        // we should not get here! either sbrk(TOOMUCH)
        // should have failed, or (with lazy allocation)
        // a pagefault should have killed this process.
        exit(1);
    }

    wait(&xstatus);
    if (xstatus == 1) {
        printf("%s: too much memory allocated!\n", s);
        exit(1);
    }

    // can one sbrk() less than a page?
    a = sbrk(0);
    for (int i = 0; i < 5000; i++) {
        b = sbrk(1);
        if (b != a) {
            printf("%s: sbrk test failed %d %p %p\n", s, i, a, b);
            exit(1);
        }
        *b = 1;
        a = b + 1;
    }
    pid = fork();
    if (pid < 0) {
        printf("%s: sbrk test fork failed\n", s);
        exit(1);
    }
    const char *c = sbrk(1);
    c = sbrk(1);
    if (c != a + 1) {
        printf("%s: sbrk test failed post-fork\n", s);
        exit(1);
    }
    if (pid == 0) {
        exit(0);
    }
    wait(&xstatus);
    exit(xstatus);
}

void sbrkmuch(const char *s) {
    enum { BIG = 100 * 1024 * 1024 };

    const char *oldbrk = sbrk(0);

    // can one grow address space to something big?
    char *a = sbrk(0);
    const uint64 amt = BIG - (uint64)a;
    const char *p = sbrk(amt);
    if (p != a) {
        printf("%s: sbrk test failed to grow big address space; enough phys mem?\n", s);
        exit(1);
    }

    const auto lastaddr = (char *)(BIG - 1);
    *lastaddr = 99;

    // can one de-allocate?
    a = sbrk(0);
    char *c = sbrk(-PGSIZE);
    if (c == SBRK_ERROR) {
        printf("%s: sbrk could not deallocate\n", s);
        exit(1);
    }
    c = sbrk(0);
    if (c != a - PGSIZE) {
        printf("%s: sbrk deallocation produced wrong address, a %p c %p\n", s, a, c);
        exit(1);
    }

    // can one re-allocate that page?
    a = sbrk(0);
    c = sbrk(PGSIZE);
    if (c != a || sbrk(0) != a + PGSIZE) {
        printf("%s: sbrk re-allocation failed, a %p c %p\n", s, a, c);
        exit(1);
    }
    if (*lastaddr == 99) {
        // should be zero
        printf("%s: sbrk de-allocation didn't really deallocate\n", s);
        exit(1);
    }

    a = sbrk(0);
    c = sbrk(-(sbrk(0) - oldbrk));
    if (c != a) {
        printf("%s: sbrk downsize failed, a %p c %p\n", s, a, c);
        exit(1);
    }
}

// can we read the kernel's memory?
void kernmem(const char *s) {
    for (auto a = (char *)KERNBASE; a < (char *)(KERNBASE + 2000000); a += 50000) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid == 0) {
            printf("%s: oops could read %p = %x\n", s, a, *a);
            exit(1);
        }
        int xstatus;
        wait(&xstatus);
        if (xstatus != -1) { // did kernel kill child?
            exit(1);
        }
    }
}

// user code should not be able to write to addresses above MAXVA.
void MAXVAplus(const char *s) {
    volatile uint64 a = MAXVA;
    for (; a != 0;) {
        const int pid = fork();
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        if (pid == 0) {
            *(char *)a = 99;
            printf("%s: oops wrote %p\n", s, (void *)a);
            exit(1);
        }
        int xstatus;
        wait(&xstatus);
        if (xstatus != -1) { // did kernel kill child?
            exit(1);
        }
        a = a << 1;
    }
}

// if we run the system out of memory, does it clean up the last
// failed allocation?
void sbrkfail(const char *s) {
    enum { BIG = 100 * 1024 * 1024 };
    int i, xstatus;
    int fds[2];
    char scratch;
    int pids[10];

    int failed = 0;
    if (pipe(fds) != 0) {
        printf("%s: pipe() failed\n", s);
        exit(1);
    }
    for (i = 0; i < static_cast<int>(sizeof(pids) / sizeof(pids[0])); i++) {
        if ((pids[i] = fork()) == 0) {
            // allocate a lot of memory
            if (sbrk(BIG - (uint64)sbrk(0)) == SBRK_ERROR) {
                write(fds[1], "0", 1);
            } else {
                write(fds[1], "1", 1);
            }
            // sit around until killed
            for (;;) {
                pause(1000);
            }
        }
        if (pids[i] != -1) {
            read(fds[0], &scratch, 1);
            if (scratch == '0') {
                failed = 1;
            }
        }
    }
    if (!failed) {
        printf("%s: no allocation failed; allocate more?\n", s);
    }

    // if those failed allocations freed up the pages they did allocate,
    // we'll be able to allocate here
    const char *c = sbrk(PGSIZE);
    for (i = 0; i < static_cast<int>(sizeof(pids) / sizeof(pids[0])); i++) {
        if (pids[i] == -1) {
            continue;
        }
        kill(pids[i]);
        wait(nullptr);
    }
    if (c == SBRK_ERROR) {
        printf("%s: failed sbrk leaked memory\n", s);
        exit(1);
    }

    // test running fork with the above allocated page
    const int pid = fork();
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    if (pid == 0) {
        // allocate a lot of memory. this should produce an error
        const char *a = sbrk(10 * BIG);
        if (a == SBRK_ERROR) {
            exit(0);
        }
        printf("%s: allocate a lot of memory succeeded %d\n", s, 10 * BIG);
        exit(1);
    }
    wait(&xstatus);
    if (xstatus != 0) {
        exit(1);
    }
}

// test reads/writes from/to allocated memory
void sbrkarg(const char *s) {
    char *a = sbrk(PGSIZE);
    const int fd = open("sbrk", O_CREATE | O_WRONLY);
    unlink("sbrk");
    if (fd < 0) {
        printf("%s: open sbrk failed\n", s);
        exit(1);
    }
    if (write(fd, a, PGSIZE) < 0) {
        printf("%s: write sbrk failed\n", s);
        exit(1);
    }
    close(fd);

    // test writes to allocated memory
    a = sbrk(PGSIZE);
    if (pipe((int *)a) != 0) {
        printf("%s: pipe() failed\n", s);
        exit(1);
    }
}

void validatetest(const char *s) {
    constexpr int hi = 1100 * 1024;
    for (uint64 p = 0; p <= static_cast<uint>(hi); p += PGSIZE) {
        // try to crash the kernel by passing in a bad string pointer
        if (link("nosuchfile", (char *)p) != -1) {
            printf("%s: link should not succeed\n", s);
            exit(1);
        }
    }
}

// does uninitialized data start out zero?
char uninit[10000];
void bsstest(const char *s) {
    for (int i = 0; i < static_cast<int>(sizeof(uninit)); i++) {
        if (uninit[i] != '\0') {
            printf("%s: bss test failed\n", s);
            exit(1);
        }
    }
}

// does exec return an error if the arguments
// are larger than a page? or does it write
// below the stack and wreck the instructions/data?
void bigargtest(const char *s) {
    int fd, xstatus;

    unlink("bigarg-ok");
    const int pid = fork();
    if (pid == 0) {
        static const char *args[MAXARG];
        char big[400];
        memset(big, ' ', sizeof(big));
        big[sizeof(big) - 1] = '\0';
        for (int i = 0; i < MAXARG - 1; i++) {
            args[i] = big;
        }
        args[MAXARG - 1] = nullptr;
        // this exec() should fail (and return) because the
        // arguments are too large.
        exec("echo", args);
        fd = open("bigarg-ok", O_CREATE);
        close(fd);
        exit(0);
    }
    if (pid < 0) {
        printf("%s: bigargtest: fork failed\n", s);
        exit(1);
    }

    wait(&xstatus);
    if (xstatus != 0) {
        exit(xstatus);
    }
    fd = open("bigarg-ok", 0);
    if (fd < 0) {
        printf("%s: bigarg test failed!\n", s);
        exit(1);
    }
    close(fd);
}

// what happens when the file system runs out of blocks?
// answer: balloc panics, so this test is not useful.
void fsfull() {
    int nfiles;
    int fsblocks = 0;

    printf("fsfull test\n");

    for (nfiles = 0;; nfiles++) {
        char name[64];
        name[0] = 'f';
        name[1] = '0' + nfiles / 1000;
        name[2] = '0' + nfiles % 1000 / 100;
        name[3] = '0' + nfiles % 100 / 10;
        name[4] = '0' + nfiles % 10;
        name[5] = '\0';
        printf("writing %s\n", name);
        const int fd = open(name, O_CREATE | O_RDWR);
        if (fd < 0) {
            printf("open %s failed\n", name);
            break;
        }
        int total = 0;
        while (true) {
            const int cc = write(fd, buf, BSIZE);
            if (cc < BSIZE) {
                break;
            }
            total += cc;
            fsblocks++;
        }
        printf("wrote %d bytes\n", total);
        close(fd);
        if (total == 0) {
            break;
        }
    }

    while (nfiles >= 0) {
        char name[64];
        name[0] = 'f';
        name[1] = '0' + nfiles / 1000;
        name[2] = '0' + nfiles % 1000 / 100;
        name[3] = '0' + nfiles % 100 / 10;
        name[4] = '0' + nfiles % 10;
        name[5] = '\0';
        unlink(name);
        nfiles--;
    }

    printf("fsfull test finished\n");
}

void argptest(const char *s) {
    const int fd = open("init", O_RDONLY);
    if (fd < 0) {
        printf("%s: open failed\n", s);
        exit(1);
    }
    read(fd, sbrk(0) - 1, -1);
    close(fd);
}

// check that there's an invalid page beneath
// the user stack, to catch stack overflow.
void stacktest(const char *s) {
    int xstatus;

    const int pid = fork();
    if (pid == 0) {
        const char *sp = (char *)xv6::r_sp();
        sp -= USERSTACK * PGSIZE;
        // the *sp should cause a trap.
        printf("%s: stacktest: read below stack %d\n", s, *sp);
        exit(1);
    }
    if (pid < 0) {
        printf("%s: fork failed\n", s);
        exit(1);
    }
    wait(&xstatus);
    if (xstatus == -1) { // kernel killed child?
        exit(0);
    }
    exit(xstatus);
}

// check that writes to a few forbidden addresses
// cause a fault, e.g. process's text and TRAMPOLINE.
void nowrite(const char *s) {
    int xstatus;
    constexpr uint64 addrs[] = {0, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                                0xffffffffffffffff};

    for (uint ai = 0; ai < sizeof(addrs) / sizeof(addrs[0]); ai++) {
        const int pid = fork();
        if (pid == 0) {
            volatile int *addr = (int *)addrs[ai];
            *addr = 10;
            printf("%s: write to %p did not fail!\n", s, addr);
            exit(0);
        }
        if (pid < 0) {
            printf("%s: fork failed\n", s);
            exit(1);
        }
        wait(&xstatus);
        if (xstatus == 0) {
            // kernel did not kill child!
            exit(1);
        }
    }
    exit(0);
}

// regression test. copyin(), copyout(), and copyinstr() used to cast
// the virtual page address to uint, which (with certain wild system
// call arguments) resulted in a kernel page faults.
auto big = (void *)0xeaeb0b5b00002f5e;
void pgbug(const char *s) {
    char *argv[1];
    argv[0] = nullptr;
    exec(static_cast<char *>(big), (const char **)argv);
    pipe(static_cast<int *>(big));

    exit(0);
}

// regression test. does the kernel panic if a process sbrk()s its
// size to be less than a page, or zero, or reduces the break by an
// amount too small to cause a page to be freed?
void sbrkbugs(const char *s) {
    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        const int sz = (uint64)sbrk(0);
        // free all user memory; there used to be a bug that
        // would not adjust p->sz correctly in this case,
        // causing exit() to panic.
        sbrk(-sz);
        // user page fault here.
        exit(0);
    }
    wait(nullptr);

    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        const int sz = (uint64)sbrk(0);
        // set the break to somewhere in the very first
        // page; there used to be a bug that would incorrectly
        // free the first page.
        sbrk(-(sz - 3500));
        exit(0);
    }
    wait(nullptr);

    pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        // set the break in the middle of a page.
        sbrk(10 * PGSIZE + 2048 - (uint64)sbrk(0));

        // reduce the break a bit, but not enough to
        // cause a page to be freed. this used to cause
        // a panic.
        sbrk(-10);

        exit(0);
    }
    wait(nullptr);

    exit(0);
}

// if process size was somewhat more than a page boundary, and then
// shrunk to be somewhat less than that page boundary, can the kernel
// still copyin() from addresses in the last page?
void sbrklast(const char *s) {
    uint64 top = (uint64)sbrk(0);
    if (top % PGSIZE != 0) {
        sbrk(PGSIZE - top % PGSIZE);
    }
    sbrk(PGSIZE);
    sbrk(10);
    sbrk(-20);
    top = (uint64)sbrk(0);
    const auto p = (char *)(top - 64);
    p[0] = 'x';
    p[1] = '\0';
    int fd = open(p, O_RDWR | O_CREATE);
    write(fd, p, 1);
    close(fd);
    fd = open(p, O_RDWR);
    p[0] = '\0';
    read(fd, p, 1);
    if (p[0] != 'x') {
        exit(1);
    }
}

// does sbrk handle signed int32 wrap-around with
// negative arguments?
void sbrk8000(const char *s) {
    sbrk(0x80000004);
    volatile char *top = sbrk(0);
    *(top - 1) = *(top - 1) + 1;
}

// regression test. test whether exec() leaks memory if one of the
// arguments is invalid. the test passes if the kernel doesn't panic.
void badarg(const char *s) {
    for (int i = 0; i < 50000; i++) {
        char *argv[2];
        argv[0] = (char *)0xffffffff;
        argv[1] = nullptr;
        exec("echo", (const char **)argv);
    }

    exit(0);
}

#define REGION_SZ (1024 * 1024 * 1024)

// Touch a page every 64 pages, which with lazy allocation
// causes one page to be allocated.
void lazy_alloc(const char *s) {
    char *i;

    char *prev_end = sbrklazy(REGION_SZ);
    if (prev_end == SBRK_ERROR) {
        printf("sbrklazy() failed\n");
        exit(1);
    }
    const char *new_end = prev_end + REGION_SZ;

    for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) {
        *(char **)i = i;
    }

    for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) {
        if (*(char **)i != i) {
            printf("failed to read value from memory\n");
            exit(1);
        }
    }

    exit(0);
}

// Touch a page every 64 pages in region, which with lazy allocation
// causes one page to be allocated. Check that freeing the region
// frees the allocated pages.
void lazy_unmap(const char *s) {
    char *i;

    char *prev_end = sbrklazy(REGION_SZ);
    if (prev_end == SBRK_ERROR) {
        printf("sbrklazy() failed\n");
        exit(1);
    }
    const char *new_end = prev_end + REGION_SZ;

    for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) {
        *(char **)i = i;
    }

    for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) {
        const int pid = fork();
        if (pid < 0) {
            printf("error forking\n");
            exit(1);
        }
        if (pid == 0) {
            sbrklazy(-1L * REGION_SZ);
            *(char **)i = i;
            exit(0);
        }
        int status;
        wait(&status);
        if (status == 0) {
            printf("memory not unmapped\n");
            exit(1);
        }
    }

    exit(0);
}

void lazy_copy(const char *s) {
    // copyinstr on lazy page
    {
        const char *p = sbrk(0);
        sbrklazy(4 * PGSIZE);
        open(p + 8192, 0);
    }

    {
        void *xx = sbrk(0);
        void *ret = sbrk(-((uint64)xx + 1));
        if (ret != xx) {
            printf("sbrk(sbrk(0)+1) returned %p, not old sz\n", ret);
            exit(1);
        }
    }

    // read() and write() to these addresses should fail.
    constexpr unsigned long bad[] = {
        0x3fffffc000,
        0x3fffffd000,
        0x3fffffe000,
        0x3ffffff000,
        0x4000000000,
        0x8000000000,
    };
    for (int i = 0; i < static_cast<int>(sizeof(bad) / sizeof(bad[0])); i++) {
        int fd = open("README", 0);
        if (fd < 0) {
            printf("cannot open README\n");
            exit(1);
        }
        if (read(fd, (char *)bad[i], 512) >= 0) {
            printf("read succeeded\n");
            exit(1);
        }
        close(fd);
        fd = open("junk", O_CREATE | O_RDWR | O_TRUNC);
        if (fd < 0) {
            printf("cannot open junk\n");
            exit(1);
        }
        if (write(fd, (char *)bad[i], 512) >= 0) {
            printf("write succeeded\n");
            exit(1);
        }
        close(fd);
    }

    exit(0);
}

void lazy_sbrk(const char *s) {
    // sbrk() takes just int, so take 2^30-sized steps towards MAXVA
    char *p = sbrk(0);
    while ((uint64)p < MAXVA - (1 << 30)) {
        p = sbrklazy(1 << 30);
        if (p == (char *)-1) {
            printf("sbrklazy(%d) returned %p\n", 1 << 30, p);
            exit(1);
        }

        p = sbrklazy(0);
    }

    const int n = TRAPFRAME - PGSIZE - (uint64)p;

    char *p1 = sbrklazy(n);
    if (p1 == (char *)-1 || p1 != p) {
        printf("sbrklazy(%d) returned %p, not expected %p\n", n, p1, p);
        exit(1);
    }

    p = sbrk(PGSIZE);
    if (p == reinterpret_cast<char *>(-1) || reinterpret_cast<uint64>(p) != TRAPFRAME - PGSIZE) {
        printf("sbrk(%d) returned %p, not expected TRAPFRAME-PGSIZE\n", PGSIZE, p);
        exit(1);
    }

    p[0] = 1;
    if (p[1] != 0) {
        printf("sbrk() returned non-zero-filled memory\n");
        exit(1);
    }

    p = sbrk(1);
    if ((uint64)p != static_cast<uint64>(-1)) {
        printf("sbrk(1) returned %p, expected error\n", p);
        exit(1);
    }

    p = sbrklazy(1);
    if ((uint64)p != static_cast<uint64>(-1)) {
        printf("sbrklazy(1) returned %p, expected error\n", p);
        exit(1);
    }

    exit(0);
}

struct test {
    void (*f)(const char *);
    const char *s;
} quicktests[] = {
    {copyin, "copyin"},
    {copyout, "copyout"},
    {copyinstr1, "copyinstr1"},
    {copyinstr2, "copyinstr2"},
    {copyinstr3, "copyinstr3"},
    {rwsbrk, "rwsbrk"},
    {truncate1, "truncate1"},
    {truncate2, "truncate2"},
    {truncate3, "truncate3"},
    {openiputtest, "openiput"},
    {exitiputtest, "exitiput"},
    {iputtest, "iput"},
    {opentest, "opentest"},
    {writetest, "writetest"},
    {writebig, "writebig"},
    {createtest, "createtest"},
    {dirtest, "dirtest"},
    {exectest, "exectest"},
    {pipe1, "pipe1"},
    {killstatus, "killstatus"},
    {preempt, "preempt"},
    {exitwait, "exitwait"},
    {reparent, "reparent"},
    {twochildren, "twochildren"},
    {forkfork, "forkfork"},
    {forkforkfork, "forkforkfork"},
    {reparent2, "reparent2"},
    {mem, "mem"},
    {sharedfd, "sharedfd"},
    {fourfiles, "fourfiles"},
    {createdelete, "createdelete"},
    {unlinkread, "unlinkread"},
    {linktest, "linktest"},
    {concreate, "concreate"},
    {linkunlink, "linkunlink"},
    {subdir, "subdir"},
    {bigwrite, "bigwrite"},
    {bigfile, "bigfile"},
    {fourteen, "fourteen"},
    {rmdot, "rmdot"},
    {dirfile, "dirfile"},
    {iref, "iref"},
    {forktest, "forktest"},
    {sbrkbasic, "sbrkbasic"},
    {sbrkmuch, "sbrkmuch"},
    {kernmem, "kernmem"},
    {MAXVAplus, "MAXVAplus"},
    {sbrkfail, "sbrkfail"},
    {sbrkarg, "sbrkarg"},
    {validatetest, "validatetest"},
    {bsstest, "bsstest"},
    {bigargtest, "bigargtest"},
    {argptest, "argptest"},
    {stacktest, "stacktest"},
    {nowrite, "nowrite"},
    {pgbug, "pgbug"},
    {sbrkbugs, "sbrkbugs"},
    {sbrklast, "sbrklast"},
    {sbrk8000, "sbrk8000"},
    {badarg, "badarg"},
    {lazy_alloc, "lazy_alloc"},
    {lazy_unmap, "lazy_unmap"},
    {lazy_copy, "lazy_copy"},
    {lazy_sbrk, "lazy_sbrk"},
    {nullptr, nullptr},
};

//
// Section with tests that take a fair bit of time
//

// directory that uses indirect blocks
void bigdir(const char *s) {
    enum { N = 500 };
    int i;
    char name[10];

    unlink("bd");

    const int fd = open("bd", O_CREATE);
    if (fd < 0) {
        printf("%s: bigdir create failed\n", s);
        exit(1);
    }
    close(fd);

    for (i = 0; i < N; i++) {
        name[0] = 'x';
        name[1] = '0' + i / 64;
        name[2] = '0' + i % 64;
        name[3] = '\0';
        if (link("bd", name) != 0) {
            printf("%s: bigdir i=%d link(bd, %s) failed\n", s, i, name);
            exit(1);
        }
    }

    unlink("bd");
    for (i = 0; i < N; i++) {
        name[0] = 'x';
        name[1] = '0' + i / 64;
        name[2] = '0' + i % 64;
        name[3] = '\0';
        if (unlink(name) != 0) {
            printf("%s: bigdir unlink failed", s);
            exit(1);
        }
    }
}

// concurrent writes to try to provoke deadlock in the virtio disk
// driver.
void manywrites(const char *s) {
    constexpr int nchildren = 4;
    constexpr int howmany = 30; // increase to look for deadlock

    for (int ci = 0; ci < nchildren; ci++) {
        const int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }

        if (pid == 0) {
            char name[3];
            name[0] = 'b';
            name[1] = 'a' + ci;
            name[2] = '\0';
            unlink(name);

            for (int iters = 0; iters < howmany; iters++) {
                for (int i = 0; i < ci + 1; i++) {
                    const int fd = open(name, O_CREATE | O_RDWR);
                    if (fd < 0) {
                        printf("%s: cannot create %s\n", s, name);
                        exit(1);
                    }
                    constexpr int sz = sizeof(buf);
                    const int cc = write(fd, buf, sz);
                    if (cc != sz) {
                        printf("%s: write(%d) ret %d\n", s, sz, cc);
                        exit(1);
                    }
                    close(fd);
                }
                unlink(name);
            }

            unlink(name);
            exit(0);
        }
    }

    for (int ci = 0; ci < nchildren; ci++) {
        int st = 0;
        wait(&st);
        if (st != 0) {
            exit(st);
        }
    }
    exit(0);
}

// regression test. does write() with an invalid buffer pointer cause
// a block to be allocated for a file that is then not freed when the
// file is deleted? if the kernel has this bug, it will panic: balloc:
// out of blocks. assumed_free may need to be raised to be more than
// the number of free blocks. this test takes a long time.
void badwrite(const char *s) {
    constexpr int assumed_free = 600;

    unlink("junk");
    for (int i = 0; i < assumed_free; i++) {
        const int fd = open("junk", O_CREATE | O_WRONLY);
        if (fd < 0) {
            printf("open junk failed\n");
            exit(1);
        }
        write(fd, (char *)0xffffffffffL, 1);
        close(fd);
        unlink("junk");
    }

    const int fd = open("junk", O_CREATE | O_WRONLY);
    if (fd < 0) {
        printf("open junk failed\n");
        exit(1);
    }
    if (write(fd, "x", 1) != 1) {
        printf("write failed\n");
        exit(1);
    }
    close(fd);
    unlink("junk");

    exit(0);
}

// test the exec() code that cleans up if it runs out
// of memory. it's really a test that such a condition
// doesn't cause a panic.
void execout(const char *s) {
    for (int avail = 0; avail < 15; avail++) {
        const int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            // allocate all of memory.
            while (true) {
                char *a = sbrk(PGSIZE);
                if (a == SBRK_ERROR) {
                    break;
                }
                *(a + PGSIZE - 1) = 1;
            }

            // free a few pages, in order to let exec() make some
            // progress.
            for (int i = 0; i < avail; i++) {
                sbrk(-PGSIZE);
            }

            close(1);
            const char *args[] = {"echo", "x", nullptr};
            exec("echo", args);
            exit(0);
        }
        wait(nullptr);
    }

    exit(0);
}

// can the kernel tolerate running out of disk space?
void diskfull(const char *s) {
    int done = 0;

    unlink("diskfulldir");

    for (int fi = 0; done == 0 && '0' + fi < 0177; fi++) {
        char name[32];
        name[0] = 'b';
        name[1] = 'i';
        name[2] = 'g';
        name[3] = '0' + fi;
        name[4] = '\0';
        unlink(name);
        const int fd = open(name, O_CREATE | O_RDWR | O_TRUNC);
        if (fd < 0) {
            // oops, ran out of inodes before running out of blocks.
            printf("%s: could not create file %s\n", s, name);
            done = 1;
            break;
        }
        for (int i = 0; i < static_cast<int>(MAXFILE); i++) {
            char buf[BSIZE];
            if (write(fd, buf, BSIZE) != BSIZE) {
                done = 1;
                close(fd);
                break;
            }
        }
        close(fd);
    }

    // now that there are no free blocks, test that dirlink()
    // merely fails (doesn't panic) if it can't extend
    // directory content. one of these file creations
    // is expected to fail.
    constexpr int nzz = 128;
    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + i / 32;
        name[3] = '0' + i % 32;
        name[4] = '\0';
        unlink(name);
        const int fd = open(name, O_CREATE | O_RDWR | O_TRUNC);
        if (fd < 0) {
            break;
        }
        close(fd);
    }

    // this mkdir() is expected to fail.
    if (mkdir("diskfulldir") == 0) {
        printf("%s: mkdir(diskfulldir) unexpectedly succeeded!\n", s);
    }

    unlink("diskfulldir");

    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + i / 32;
        name[3] = '0' + i % 32;
        name[4] = '\0';
        unlink(name);
    }

    for (int i = 0; '0' + i < 0177; i++) {
        char name[32];
        name[0] = 'b';
        name[1] = 'i';
        name[2] = 'g';
        name[3] = '0' + i;
        name[4] = '\0';
        unlink(name);
    }
}

void outofinodes(const char *s) {
    constexpr int nzz = 32 * 32;
    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + i / 32;
        name[3] = '0' + i % 32;
        name[4] = '\0';
        unlink(name);
        const int fd = open(name, O_CREATE | O_RDWR | O_TRUNC);
        if (fd < 0) {
            // failure is eventually expected.
            break;
        }
        close(fd);
    }

    for (int i = 0; i < nzz; i++) {
        char name[32];
        name[0] = 'z';
        name[1] = 'z';
        name[2] = '0' + i / 32;
        name[3] = '0' + i % 32;
        name[4] = '\0';
        unlink(name);
    }
}

test slowtests[] = {
    {bigdir, "bigdir"},
    {manywrites, "manywrites"},
    {badwrite, "badwrite"},
    {execout, "execout"},
    {diskfull, "diskfull"},
    {outofinodes, "outofinodes"},

    {nullptr, nullptr},
};

//
// drive tests
//

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int run(void f(const char *), const char *s) {
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if ((pid = fork()) < 0) {
        printf("runtest: fork error\n");
        exit(1);
    }
    if (pid == 0) {
        f(s);
        exit(0);
    }
    wait(&xstatus);
    if (xstatus != 0) {
        printf("FAILED\n");
    } else {
        printf("OK\n");
    }
    return xstatus == 0;
}

int runtests(test *tests, char *justone, const int continuous) {
    int ntests = 0;
    for (const test *t = tests; t->s != nullptr; t++) {
        if (justone == nullptr || strcmp(t->s, justone) == 0) {
            ntests++;
            if (!run(t->f, t->s)) {
                if (continuous != 2) {
                    printf("SOME TESTS FAILED\n");
                    return -1;
                }
            }
        }
    }
    return ntests;
}

// use sbrk() to count how many free physical memory pages there are.
int countfree() {
    int n = 0;
    const uint64 sz0 = reinterpret_cast<uint64>(sbrk(0));
    while (true) {
        const char *a = sbrk(PGSIZE);
        if (a == SBRK_ERROR) {
            break;
        }
        n += 1;
    }
    sbrk(-((uint64)sbrk(0) - sz0));
    return n;
}

int drivetests(const int quick, const int continuous, char *justone) {
    do {
        printf("usertests starting\n");
        const int free0 = countfree();
        int free1 = 0;
        int ntests = 0;
        int n = runtests(quicktests, justone, continuous);
        if (n < 0) {
            if (continuous != 2) {
                return 1;
            }
        } else {
            ntests += n;
        }
        if (!quick) {
            if (justone == nullptr) {
                printf("usertests slow tests starting\n");
            }
            n = runtests(slowtests, justone, continuous);
            if (n < 0) {
                if (continuous != 2) {
                    return 1;
                }
            } else {
                ntests += n;
            }
        }
        if ((free1 = countfree()) < free0) {
            printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0);
            if (continuous != 2) {
                return 1;
            }
        }
        if (justone != nullptr && ntests == 0) {
            printf("NO TESTS EXECUTED\n");
            return 1;
        }
    } while (continuous);
    return 0;
}

int main(const int argc, char *argv[]) {
    int continuous = 0;
    int quick = 0;
    char *justone = nullptr;

    if (argc == 2 && strcmp(argv[1], "-q") == 0) {
        quick = 1;
    } else if (argc == 2 && strcmp(argv[1], "-c") == 0) {
        continuous = 1;
    } else if (argc == 2 && strcmp(argv[1], "-C") == 0) {
        continuous = 2;
    } else if (argc == 2 && argv[1][0] != '-') {
        justone = argv[1];
    } else if (argc > 1) {
        printf("Usage: usertests [-c] [-C] [-q] [testname]\n");
        exit(1);
    }
    if (drivetests(quick, continuous, justone)) {
        exit(1);
    }
    printf("ALL TESTS PASSED\n");
    exit(0);
}
