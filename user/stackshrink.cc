// Demonstrate dynamic stack shrink: after deep stack use unwinds, heap growth
// that would collide with the old stack_bottom succeeds because unused stack
// pages are reclaimed in sys_sbrk().

#include "kernel/lib/types.h"
#include "kernel/lib/param.h"
#include "kernel/arch/riscv/riscv.h"
#include "kernel/arch/riscv/memlayout.h"
#include "user/user.h"

// Extra stack pages mapped below the initial one-page stack.
static constexpr int EXTRA_STACK_PAGES = 24;
// One large frame per recursive call, so each call uses a new stack page.
static constexpr int FRAME_SIZE = 4000;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
#pragma GCC push_options
#pragma GCC optimize("O0")

static void burn_stack_pages(const int remaining) {
    volatile char frame[FRAME_SIZE];
    for (int i = 0; i < FRAME_SIZE; i++) {
        frame[i] = static_cast<char>(remaining ^ i);
    }
    if (frame[0] == 0 && remaining < 0) {
        return;
    }
    if (remaining > 1) {
        burn_stack_pages(remaining - 1);
    }
}

#pragma GCC pop_options
#pragma GCC diagnostic pop

// Extend the lazy heap break up to target, same stepping strategy as lazy_sbrk.
static char *grow_heap_to(const uint64 target) {
    char *p = sbrk(0);
    if (p == SBRK_ERROR) {
        return SBRK_ERROR;
    }

    while (reinterpret_cast<uint64>(p) < target - (1UL << 30)) {
        p = sbrklazy(1 << 30);
        if (p == SBRK_ERROR) {
            return SBRK_ERROR;
        }
        p = sbrklazy(0);
        if (p == SBRK_ERROR) {
            return SBRK_ERROR;
        }
    }

    const int step = static_cast<int>(target - reinterpret_cast<uint64>(p));
    p = sbrklazy(step);
    if (p == SBRK_ERROR) {
        return SBRK_ERROR;
    }
    return sbrk(0);
}

int main() {
    const uint64 stack_bottom0 = USERSTACK_HIGH - USERSTACK * PGSIZE;

    // After burn_stack_pages(N), about N-1 new stack pages sit below the
    // initial stack page. Position the heap one guard page below that.
    const uint64 heap_target =
        stack_bottom0 - static_cast<uint64>(EXTRA_STACK_PAGES) * PGSIZE;
    printf("stackshrink: extending heap break toward %p\n",
           reinterpret_cast<void *>(heap_target));
    if (grow_heap_to(heap_target) == SBRK_ERROR) {
        printf("stackshrink: grow_heap_to failed\n");
        exit(1);
    }

    char *heap = sbrk(0);
    if (reinterpret_cast<uint64>(heap) != heap_target) {
        printf("stackshrink: heap at %p, expected %p\n", heap,
               reinterpret_cast<void *>(heap_target));
        exit(1);
    }

    printf("stackshrink: using %d extra stack pages\n", EXTRA_STACK_PAGES);
    burn_stack_pages(EXTRA_STACK_PAGES);

    // burn_stack_pages(N) maps about N-1 new stack pages. Without shrink the
    // heap is already at the limit; with shrink those pages become heap space.
    const int alloc_bytes = (EXTRA_STACK_PAGES - 1) * PGSIZE;
    printf("stackshrink: requesting %d bytes (%d pages) after stack unwind\n",
           alloc_bytes, EXTRA_STACK_PAGES - 1);
    char *extra = sbrklazy(alloc_bytes);
    if (extra == SBRK_ERROR) {
        printf("stackshrink: FAILED: sbrklazy(%d) after stack shrink "
               "should succeed\n",
               alloc_bytes);
        exit(1);
    }
    if (reinterpret_cast<uint64>(extra) != heap_target) {
        printf("stackshrink: sbrklazy returned %p, expected %p\n", extra, heap);
        exit(1);
    }

    extra[0] = 0xab;
    extra[alloc_bytes - 1] = 0xcd;
    if (extra[0] != static_cast<char>(0xab) ||
        extra[alloc_bytes - 1] != static_cast<char>(0xcd)) {
        printf("stackshrink: cannot touch newly allocated heap\n");
        exit(1);
    }

    printf("stackshrink: OK\n");
    exit(0);
}
