// Tiny program exec'd by exec_test to inspect post-exec state.
#include "kernel/lib/types.h"
#include "kernel/lib/param.h"
#include "kernel/arch/riscv/riscv.h"
#include "user/user.h"

static uint64 read_sp() {
    uint64 x;
    asm volatile("mv %0, sp" : "=r"(x));
    return x;
}

int main(const int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "stackprobe") == 0) {
        const char *sp = reinterpret_cast<char *>(read_sp());
        sp -= USERSTACK * PGSIZE;
        printf("exec_target: read below stack %d\n", *sp);
        exit(1);
    }

    printf("%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("%s\n", argv[i]);
    }
    printf("heap %p\n", sbrk(0));
    exit(0);
}
