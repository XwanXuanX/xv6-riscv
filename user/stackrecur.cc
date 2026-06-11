// Stress the user stack with deep recursion.
// Prints the current depth on every call; with a single-page stack the process
// dies quickly on a guard-page fault. After a growable stack is implemented,
// the maximum depth before failure should increase sharply.

#include "user/user.h"

// ignore infinite recursion since it is intended
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"

// disable any optimizations so recursion remains recursion
#pragma GCC push_options
#pragma GCC optimize("O0")

static void print_depth(const int depth) {
    char buf[16];
    int i = 0;
    int d = depth;

    if (d == 0) {
        buf[i++] = '0';
    } else {
        char rev[16];
        int j = 0;
        while (d > 0) {
            rev[j++] = '0' + (d % 10);
            d /= 10;
        }
        while (j > 0) {
            buf[i++] = rev[--j];
        }
    }
    buf[i++] = '\n';
    write(1, buf, i);
}

typedef void (*recur_fn)(int);

static void recurse_impl(const int depth) {
    volatile char frame[256];
    for (int i = 0; i < static_cast<int>(sizeof(frame)); i++) {
        frame[i] = static_cast<char>(depth);
    }

    print_depth(depth);
    recurse_impl(depth + 1);
}

#pragma GCC pop_options
#pragma GCC diagnostic pop

static void recurse(const int depth) { recurse_impl(depth); }

int main() {
    printf("stackrecur: starting (frame size ~256 bytes)\n");
    recurse(0);
    exit(0);
}
