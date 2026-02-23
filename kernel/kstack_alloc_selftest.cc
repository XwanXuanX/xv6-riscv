// This is a kernel self test at boot time

#include "kernel/defs.h"
#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/kstack_alloc.h"
#include "utility/assert.h"

#include <array>

namespace xv6::test {

static uint32 xor_shift32(uint32 &x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static bool kva_sane(void *va) {
    const auto a = reinterpret_cast<uint64>(va);
    if (a == 0) {
        return false;
    }
    if (a % PGSIZE != 0) {
        return false;
    }
    if (a >= TRAMPOLINE) {
        return false;
    }

    // Optional stronger check:
    // KSTACK(i) gives the stack page VA for slot i.
    // Valid kva must equal some KSTACK(i).
    // This is O(NPROC); fine for a boot-time test.
    for (int i = 0; i < NPROC; i++) {
        if (a == static_cast<uint64>(KSTACK(i))) {
            return true;
        }
    }
    return false;
}

static void shuffle_ptrs(std::array<void *, NPROC> &ptrs, uint32 seed) {
    for (int i = NPROC - 1; i > 0; --i) {
        const int j =
            static_cast<int>(xor_shift32(seed) % static_cast<uint32>(i + 1));
        void *tmp = ptrs[i];
        ptrs[i] = ptrs[j];
        ptrs[j] = tmp;
    }
}

static void check_no_duplicates(const std::array<void *, NPROC> &ptrs) {
    for (int i = 0; i < NPROC; i++) {
        for (int j = i + 1; j < NPROC; j++) {
            if (ptrs[i] == ptrs[j]) {
                panic("kstack selftest: duplicate kva");
            }
        }
    }
}

static void one_round_alloc_all(std::array<void *, NPROC> &out) {
    auto &ksa = kstack_allocator::instance();

    // allocate NPROC stacks
    for (int i = 0; i < NPROC; i++) {
        void *va = ksa.alloc();
        if (!va) {
            panic("kstack selftest: alloc returned null early");
        }
        assert(kva_sane(va), "kstack selftest: bad kva (alignment/range/slot)");
        out[i] = va;
    }

    // allocator should now be empty
    assert(ksa.alloc() == nullptr,
           "kstack selftest: expected nullptr when empty");

    // uniqueness
    check_no_duplicates(out);
}

static void one_round_free_all(const std::array<void *, NPROC> &in) {
    auto &ksa = kstack_allocator::instance();
    for (int i = 0; i < NPROC; i++) {
        assert(in[i] != nullptr, "kstack selftest: freeing nullptr");
        ksa.free(in[i]);
    }
}

void kstack_self_test() {
    auto &ksa = kstack_allocator::instance();

    // Storage for returned KVAs (static to avoid kernel stack overflow)
    static std::array<void *, NPROC> kv1{};
    static std::array<void *, NPROC> kv2{};
    {
        // Phase 1: fresh init, allocate all, free all
        ksa.init();
        one_round_alloc_all(kv1);
        one_round_free_all(kv1);

        // after freeing all, we should be able to allocate all again
        one_round_alloc_all(kv2);

        // kv2 should also be unique and sane; already checked.
        // It's allowed (and likely) that kv2 is a permutation of kv1.

        // free in randomized order
        constexpr uint32 seed = 0xC0FFEEu ^ 0x5A5A1234u;
        shuffle_ptrs(kv2, seed);
        one_round_free_all(kv2);
    }
    {
        // Phase 2: stress multiple cycles with random free order
        for (int round = 0; round < 8; ++round) {
            ksa.init();

            one_round_alloc_all(kv1);

            // randomize freeing order
            const uint32 s = 0x9E3779B9u ^ static_cast<uint32>(round * 1337);
            shuffle_ptrs(kv1, s);
            one_round_free_all(kv1);

            // allocate all again without re-init (should be full)
            one_round_alloc_all(kv2);

            // free again
            shuffle_ptrs(kv2, s ^ 0xDEADBEEFu);
            one_round_free_all(kv2);
        }
    }
    {
        // Phase 3: partial allocate/free interleaving
        // Allocate half, free quarter, allocate quarter, etc.
        ksa.init();

        // allocate all first into kv1
        one_round_alloc_all(kv1);

        // free all back (so we start from a known full state)
        one_round_free_all(kv1);

        // now do interleaving
        // allocate NPROC/2
        for (int i = 0; i < NPROC / 2; i++) {
            void *va = ksa.alloc();
            assert(va != nullptr, "kstack selftest: alloc null in interleave");
            assert(kva_sane(va), "kstack selftest: bad kva in interleave");
            kv1[i] = va;
        }

        // free NPROC/4 of those
        for (int i = 0; i < NPROC / 4; i++) {
            ksa.free(kv1[i]);
            kv1[i] = nullptr;
        }

        // allocate NPROC/4 again; should all succeed
        for (int i = 0; i < NPROC / 4; i++) {
            void *va = ksa.alloc();
            assert(va != nullptr, "kstack selftest: alloc2 null in interleave");
            assert(kva_sane(va), "kstack selftest: bad kva2 in interleave");
            kv2[i] = va;
        }

        // Now count how many outstanding we have and free them all:
        // outstanding from kv1: indices [NPROC/4 .. NPROC/2)
        for (int i = NPROC / 4; i < NPROC / 2; i++) {
            ksa.free(kv1[i]);
            kv1[i] = nullptr;
        }
        // outstanding from kv2: indices [0 .. NPROC/4)
        for (int i = 0; i < NPROC / 4; i++) {
            ksa.free(kv2[i]);
            kv2[i] = nullptr;
        }

        // allocator should be back to full now; allocate all to confirm
        one_round_alloc_all(kv1);
        one_round_free_all(kv1);
    }

    printf("kstack_alloc selftest: OK\n");
}

} // namespace xv6::test