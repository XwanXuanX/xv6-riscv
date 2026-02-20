// This is a kernel self test at boot time

#include "defs.h"
#include "slab.h"
#include <array>

namespace xv6::test {

static uint32 xor_shift32(uint32 &x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

template <uint64 S, int N> static void slab_test_one() {
    auto &slab = slab_allocator<S>::instance();

    static std::array<void *, N> ptrs{};

    // allocate N objects
    for (int i = 0; i < N; i++) {
        void *p = slab.alloc();
        if (!p) {
            panic("slab selftest: alloc returned null");
        }

        // basic alignment sanity
        if ((reinterpret_cast<uint64>(p) & (alignof(void *) - 1)) != 0) {
            panic("slab selftest: bad alignment");
        }

        ptrs[i] = p;
    }

    // check duplicates
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            if (ptrs[i] == ptrs[j]) {
                panic("slab selftest: duplicate pointer");
            }
        }
    }

    // shuffle + free in pseudo-random order
    uint32 seed = 0xC0FFEEu ^ static_cast<uint32>(S);
    for (int i = N - 1; i > 0; --i) {
        int j =
            static_cast<int>(xor_shift32(seed) % static_cast<uint32>(i + 1));
        void *tmp = ptrs[i];
        ptrs[i] = ptrs[j];
        ptrs[j] = tmp;
    }
    for (int i = 0; i < N; i++) {
        slab.free(ptrs[i]);
    }

    // allocate again
    for (int i = 0; i < N; i++) {
        void *p = slab.alloc();
        if (!p) {
            panic("slab selftest: alloc2 returned null");
        }
        ptrs[i] = p;
    }

    // free again
    for (int i = 0; i < N; i++) {
        slab.free(ptrs[i]);
    }
}

void slab_self_test() {
    {
        slab_test_one<32, 1024>();
        slab_allocator<32>::instance().reclaim();
    }
    {
        slab_test_one<64, 1024>();
        slab_allocator<64>::instance().reclaim();
    }
    {
        slab_test_one<128, 1024>();
        slab_allocator<128>::instance().reclaim();
    }
    {
        slab_test_one<256, 512>();
        slab_allocator<256>::instance().reclaim();
    }
    {
        slab_test_one<512, 512>();
        slab_allocator<512>::instance().reclaim();
    }
    printf("slab selftest: OK\n");
}

} // namespace xv6::test