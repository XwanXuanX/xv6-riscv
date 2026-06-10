// Standalone tests for the exec() / kexec() system call.
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "user/user.h"

#include <array>

static int str_prefix(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static uint64 parse_hex(const char *s) {
    uint64 val = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 16 + static_cast<uint64>(*s - '0');
        } else if (*s >= 'a' && *s <= 'f') {
            val = val * 16 + static_cast<uint64>(*s - 'a' + 10);
        } else if (*s >= 'A' && *s <= 'F') {
            val = val * 16 + static_cast<uint64>(*s - 'A' + 10);
        } else {
            break;
        }
        s++;
    }
    return val;
}

static int failures = 0;

static void pass(const char *name) { printf("exec_test: %s OK\n", name); }

static void fail_status(const char *name, const char *msg, const int xstatus) {
    printf("exec_test: %s FAILED: %s (child xstatus=%d)\n", name, msg, xstatus);
    failures++;
}

static void fail(const char *name, const char *msg) {
    fail_status(name, msg, -1);
}

// Run path with stdout captured into buf. Returns 0 on success, -1 on setup
// error. Child exit status is stored in *xstatus.
static int
run_capture_with_setup(const char *path, const char **argv, char *buf,
                       const int buflen, int *xstatus, void (*setup)(void)) {
    int pfd[2];
    if (pipe(pfd) < 0) {
        return -1;
    }

    const int pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pfd[0]);
        close(1);
        if (dup(pfd[1]) != 1) {
            exit(98);
        }
        close(pfd[1]);
        if (setup) {
            setup();
        }
        exec(path, argv);
        exit(99);
    }

    close(pfd[1]);
    int n = 0;
    while (n < buflen - 1) {
        const int m = read(pfd[0], buf + n, buflen - 1 - n);
        if (m <= 0) {
            break;
        }
        n += m;
    }
    buf[n] = '\0';
    close(pfd[0]);

    if (wait(xstatus) != pid) {
        return -1;
    }
    return 0;
}

static int run_capture(const char *path, const char **argv, char *buf,
                       const int buflen, int *xstatus) {
    return run_capture_with_setup(path, argv, buf, buflen, xstatus, nullptr);
}

static uint64 parse_heap_line(const char *out) {
    const char *p = out;
    while ((p = strchr(p, '\n')) != nullptr) {
        p++;
        if (str_prefix(p, "heap ")) {
            return parse_hex(p + 5);
        }
    }
    return static_cast<uint64>(-1);
}

static void test_invalid_path() {
    const char *argv[] = {"x", nullptr};
    if (exec("/no/such/file", argv) != -1) {
        fail("invalid_path", "exec returned success");
        return;
    }
    pass("invalid_path");
}

static void test_not_elf() {
    unlink("exec-not-elf");
    const int fd = open("exec-not-elf", O_CREATE | O_RDWR);
    if (fd < 0) {
        fail("not_elf", "create failed");
        return;
    }
    write(fd, "not an elf", 10);
    close(fd);

    const char *argv[] = {"x", nullptr};
    if (exec("exec-not-elf", argv) != -1) {
        fail("not_elf", "exec returned success");
        unlink("exec-not-elf");
        return;
    }
    unlink("exec-not-elf");
    pass("not_elf");
}

static void test_basic_echo() {
    unlink("exec-echo-ok");
    const int pid = fork();
    if (pid < 0) {
        fail("basic_echo", "fork failed");
        return;
    }
    if (pid == 0) {
        close(1);
        const int fd = open("exec-echo-ok", O_CREATE | O_WRONLY);
        if (fd != 1) {
            exit(97);
        }
        const char *argv[] = {"echo", "OK", nullptr};
        exec("echo", argv);
        exit(99);
    }

    int xstatus = 0;
    if (wait(&xstatus) != pid || xstatus != 0) {
        fail_status("basic_echo", "child failed (99=exec returned)", xstatus);
        return;
    }

    const int fd = open("exec-echo-ok", O_RDONLY);
    if (fd < 0) {
        fail("basic_echo", "open result failed");
        return;
    }
    char buf[3] = {};
    if (read(fd, buf, 2) != 2) {
        fail("basic_echo", "read result failed");
        close(fd);
        unlink("exec-echo-ok");
        return;
    }
    close(fd);
    unlink("exec-echo-ok");

    if (buf[0] != 'O' || buf[1] != 'K') {
        fail("basic_echo", "wrong output");
        return;
    }
    pass("basic_echo");
}

static int line_equals(const char *line, const char *expect) {
    const uint n = strlen(expect);
    return str_prefix(line, expect) && (line[n] == '\n' || line[n] == '\0');
}

static void test_argc_argv() {
    std::array<char, 512> out{};
    const char *argv[] = {"exec_target", "alpha", "beta", nullptr};
    int xstatus = 0;

    if (run_capture("exec_target", argv, out.data(), out.size(), &xstatus) <
        0) {
        fail("argc_argv", "run_capture failed");
        return;
    }
    if (xstatus != 0) {
        fail_status("argc_argv", "child exit status (99=exec returned)",
                    xstatus);
        return;
    }

    char *lines[5] = {};
    lines[0] = out.data();
    for (int i = 1; i < 5; i++) {
        char *nl = strchr(lines[i - 1], '\n');
        if (nl == nullptr) {
            fail("argc_argv", "short output");
            return;
        }
        lines[i] = nl + 1;
    }

    if (atoi(lines[0]) != 3) {
        fail("argc_argv", "bad argc");
        return;
    }
    if (!line_equals(lines[1], "exec_target")) {
        printf("exec_test: argc_argv argv[0] got '%s'\n", lines[1]);
        fail("argc_argv", "bad argv[0]");
        return;
    }
    if (!line_equals(lines[2], "alpha")) {
        fail("argc_argv", "bad argv[1]");
        return;
    }
    if (!line_equals(lines[3], "beta")) {
        fail("argc_argv", "bad argv[2]");
        return;
    }
    if (parse_heap_line(out.data()) == static_cast<uint64>(-1)) {
        fail("argc_argv", "missing heap line");
        return;
    }
    pass("argc_argv");
}

static void grow_heap_before_exec() {
    if (sbrk(16 * PGSIZE) == SBRK_ERROR) {
        exit(96);
    }
}

static void test_heap_reset() {
    std::array<char, 256> out{};
    const char *argv[] = {"exec_target", "after-sbrk", nullptr};
    int xstatus = 0;

    if (run_capture_with_setup("exec_target", argv, out.data(), out.size(),
                               &xstatus, grow_heap_before_exec) < 0) {
        fail("heap_reset", "run_capture failed");
        return;
    }
    if (xstatus != 0) {
        fail_status("heap_reset", "child failed (99=exec returned)", xstatus);
        return;
    }

    const uint64 heap = parse_heap_line(out.data());
    if (heap == static_cast<uint64>(-1)) {
        fail("heap_reset", "missing heap line");
        return;
    }
    if (heap > 256 * PGSIZE) {
        printf("exec_test: heap_reset heap=%p too large\n",
               reinterpret_cast<void *>(heap));
        fail("heap_reset", "heap not reset");
        return;
    }
    pass("heap_reset");
}

static void test_too_many_args() {
    const int pid = fork();
    if (pid < 0) {
        fail("too_many_args", "fork failed");
        return;
    }
    if (pid == 0) {
        static const char *args[MAXARG + 2];
        static char word[] = "x";
        for (int i = 0; i < MAXARG + 1; i++) {
            args[i] = word;
        }
        args[MAXARG + 1] = nullptr;
        exec("echo", args);
        exit(0);
    }

    int xstatus = 0;
    wait(&xstatus);
    if (xstatus == 0) {
        pass("too_many_args");
        return;
    }
    fail_status("too_many_args", "unexpected child status", xstatus);
}

static void test_big_args_fail() {
    unlink("exec-bigarg-ok");
    const int pid = fork();
    if (pid < 0) {
        fail("big_args_fail", "fork failed");
        return;
    }
    if (pid == 0) {
        static const char *args[MAXARG];
        static char big[400];
        memset(big, ' ', sizeof(big));
        big[sizeof(big) - 1] = '\0';
        for (int i = 0; i < MAXARG - 1; i++) {
            args[i] = big;
        }
        args[MAXARG - 1] = nullptr;
        exec("echo", args);
        const int fd = open("exec-bigarg-ok", O_CREATE);
        close(fd);
        exit(0);
    }

    int xstatus = 0;
    wait(&xstatus);
    if (open("exec-bigarg-ok", O_RDONLY) >= 0) {
        unlink("exec-bigarg-ok");
        pass("big_args_fail");
        return;
    }
    fail("big_args_fail", "exec replaced image despite oversized argv");
}

static void test_fd_preserved() {
    unlink("exec-fd-ok");
    const int pid = fork();
    if (pid < 0) {
        fail("fd_preserved", "fork failed");
        return;
    }
    if (pid == 0) {
        const int fd = open("exec-fd-ok", O_CREATE | O_RDWR);
        if (fd < 0) {
            exit(95);
        }
        write(fd, "XY", 2);
        const char *argv[] = {"exec_target", "fd", nullptr};
        exec("exec_target", argv);
        exit(99);
    }

    int xstatus = 0;
    if (wait(&xstatus) != pid || xstatus != 0) {
        fail_status("fd_preserved", "child failed (99=exec returned)", xstatus);
        return;
    }

    const int fd = open("exec-fd-ok", O_RDONLY);
    if (fd < 0) {
        fail("fd_preserved", "result file missing");
        return;
    }
    char buf[3] = {};
    const int n = read(fd, buf, 2);
    close(fd);
    unlink("exec-fd-ok");
    if (n != 2 || buf[0] != 'X' || buf[1] != 'Y') {
        fail("fd_preserved", "file contents wrong after exec");
        return;
    }
    pass("fd_preserved");
}

static void test_stack_guard() {
    const char *argv[] = {"exec_target", "stackprobe", nullptr};
    const int pid = fork();
    if (pid < 0) {
        fail("stack_guard", "fork failed");
        return;
    }
    if (pid == 0) {
        exec("exec_target", argv);
        exit(99);
    }

    int xstatus = 0;
    if (wait(&xstatus) != pid) {
        fail("stack_guard", "wait failed");
        return;
    }
    if (xstatus != -1) {
        fail_status("stack_guard",
                    "child was not killed on guard violation "
                    "(99=exec returned)",
                    xstatus);
        return;
    }
    pass("stack_guard");
}

static void test_repeated_exec() {
    for (int i = 0; i < 20; i++) {
        std::array<char, 128> out{};
        const char *argv[] = {"exec_target", "repeat", nullptr};
        int xstatus = 0;
        if (run_capture("exec_target", argv, out.data(), out.size(), &xstatus) <
                0 ||
            xstatus != 0) {
            fail("repeated_exec", "iteration failed");
            return;
        }
    }
    pass("repeated_exec");
}

static void test_path_variants() {
    std::array<char, 128> out{};
    const char *argv[] = {"exec_target", "path", nullptr};
    int xstatus = 0;

    if (run_capture("./exec_target", argv, out.data(), out.size(), &xstatus) <
            0 ||
        xstatus != 0) {
        fail("path_variants", "./exec_target failed");
        return;
    }
    if (run_capture("/exec_target", argv, out.data(), out.size(), &xstatus) <
            0 ||
        xstatus != 0) {
        fail("path_variants", "/exec_target failed");
        return;
    }
    pass("path_variants");
}

int main() {
    printf("exec_test starting\n");

    test_invalid_path();
    test_not_elf();
    test_basic_echo();
    test_argc_argv();
    test_heap_reset();
    test_too_many_args();
    test_big_args_fail();
    test_fd_preserved();
    test_stack_guard();
    test_repeated_exec();
    test_path_variants();

    if (failures == 0) {
        printf("exec_test: all tests passed\n");
        exit(0);
    }

    printf("exec_test: %d test(s) failed\n", failures);
    exit(1);
}
