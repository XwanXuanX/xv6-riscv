// Boot-time self test for the dynamic kernel stack KVA allocator.

#include "kernel/defs.h"
#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/kstack_alloc.h"
#include "kernel/util/assert.h"

#include <array>

namespace xv6::test {

// Batch size for alloc/free stress (greater than the old fixed NPROC=64 cap).
static constexpr int KSTACK_SELFTEST_BATCH = 128;

static uint32 xor_shift32(uint32 &x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static bool kva_sane(void *va) {
    return kstack_kva_valid(reinterpret_cast<uint64>(va));
}

static void
shuffle_ptrs(std::array<void *, KSTACK_SELFTEST_BATCH> &ptrs, uint32 seed) {
    for (int i = KSTACK_SELFTEST_BATCH - 1; i > 0; --i) {
        const int j =
            static_cast<int>(xor_shift32(seed) % static_cast<uint32>(i + 1));
        void *tmp = ptrs[i];
        ptrs[i] = ptrs[j];
        ptrs[j] = tmp;
    }
}

static void
check_no_duplicates(const std::array<void *, KSTACK_SELFTEST_BATCH> &ptrs,
                    int count) {
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (ptrs[i] == ptrs[j]) {
                panic("kstack selftest: duplicate kva");
            }
        }
    }
}

static int one_round_alloc_all(std::array<void *, KSTACK_SELFTEST_BATCH> &out) {
    auto &ksa = kstack_allocator::instance();
    int n = 0;
    for (int i = 0; i < KSTACK_SELFTEST_BATCH; i++) {
        void *va = ksa.alloc();
        if (!va) {
            break;
        }
        assert(kva_sane(va), "kstack selftest: bad kva (alignment/range/slot)");
        out[i] = va;
        n++;
    }
    assert(n > 0, "kstack selftest: could not allocate any kva");
    check_no_duplicates(out, n);
    return n;
}

static void
one_round_free_all(const std::array<void *, KSTACK_SELFTEST_BATCH> &in,
                   int count) {
    auto &ksa = kstack_allocator::instance();
    for (int i = 0; i < count; i++) {
        assert(in[i] != nullptr, "kstack selftest: freeing nullptr");
        ksa.free(in[i]);
    }
}

void kstack_self_test() {
    auto &ksa = kstack_allocator::instance();

    static std::array<void *, KSTACK_SELFTEST_BATCH> kv1{};
    static std::array<void *, KSTACK_SELFTEST_BATCH> kv2{};

    {
        ksa.init();
        const int n1 = one_round_alloc_all(kv1);
        one_round_free_all(kv1, n1);

        const int n2 = one_round_alloc_all(kv2);
        assert(n2 == n1, "kstack selftest: alloc count changed after recycle");

        constexpr uint32 seed = 0xC0FFEEu ^ 0x5A5A1234u;
        shuffle_ptrs(kv2, seed);
        one_round_free_all(kv2, n2);
    }
    {
        for (int round = 0; round < 8; ++round) {
            ksa.init();

            const int n1 = one_round_alloc_all(kv1);
            const uint32 s = 0x9E3779B9u ^ static_cast<uint32>(round * 1337);
            shuffle_ptrs(kv1, s);
            one_round_free_all(kv1, n1);

            const int n2 = one_round_alloc_all(kv2);
            shuffle_ptrs(kv2, s ^ 0xDEADBEEFu);
            one_round_free_all(kv2, n2);
        }
    }
    {
        ksa.init();
        const int n = one_round_alloc_all(kv1);
        one_round_free_all(kv1, n);

        const int half = n / 2;
        const int quarter = n / 4;

        for (int i = 0; i < half; i++) {
            void *va = ksa.alloc();
            assert(va != nullptr, "kstack selftest: alloc null in interleave");
            assert(kva_sane(va), "kstack selftest: bad kva in interleave");
            kv1[i] = va;
        }

        for (int i = 0; i < quarter; i++) {
            ksa.free(kv1[i]);
            kv1[i] = nullptr;
        }

        for (int i = 0; i < quarter; i++) {
            void *va = ksa.alloc();
            assert(va != nullptr, "kstack selftest: alloc2 null in interleave");
            assert(kva_sane(va), "kstack selftest: bad kva2 in interleave");
            kv2[i] = va;
        }

        for (int i = quarter; i < half; i++) {
            ksa.free(kv1[i]);
            kv1[i] = nullptr;
        }
        for (int i = 0; i < quarter; i++) {
            ksa.free(kv2[i]);
            kv2[i] = nullptr;
        }

        const int n2 = one_round_alloc_all(kv1);
        assert(n2 == n, "kstack selftest: full recycle count mismatch");
        one_round_free_all(kv1, n2);
    }
    {
        // Bump past the old NPROC=64 limit without re-init.
        ksa.init();
        static std::array<void *, 192> many{};
        int count = 0;
        for (int i = 0; i < static_cast<int>(many.size()); i++) {
            void *va = ksa.alloc();
            if (!va) {
                break;
            }
            assert(kva_sane(va), "kstack selftest: bad kva in bump test");
            many[i] = va;
            count++;
        }
        assert(count > 64,
               "kstack selftest: expected more than 64 bump allocations");
        for (int i = 0; i < count; i++) {
            ksa.free(many[i]);
        }
    }
}

} // namespace xv6::test