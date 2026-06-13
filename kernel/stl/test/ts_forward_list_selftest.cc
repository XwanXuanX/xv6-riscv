// This is a kernel self test at boot time

#include "kernel/lib/defs.h"
#include "kernel/lib/types.h"
#include "kernel/sync/spinlock.h"
#include "kernel/mm/slab.h"
#include "kernel/util/lock_guard.h"
#include "kernel/util/assert.h"
#include "kernel/stl/ts_forward_list.h"

#include <array>

namespace xv6::test {

static uint32 xor_shift32(uint32 &x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// A tiny forward_list model using a fixed array as storage.
// This avoids dynamic allocation and gives us a "golden" behavior.
//
// Representation:
//  - buf[0...len) holds the list elements in order (front at index 0).
struct model_list {
    static constexpr int MAX = 2048;
    std::array<int, MAX> buf{};
    int len = 0;

    void clear() { len = 0; }
    [[nodiscard]] bool empty() const { return len == 0; }
    [[nodiscard]] int front() const {
        assert(len > 0, "model front on empty");
        return buf[0];
    }

    void push_front(const int v) {
        assert(len < MAX, "model overflow");
        for (int i = len; i > 0; --i) {
            buf[i] = buf[i - 1];
        }
        buf[0] = v;
        ++len;
    }

    bool pop_front() {
        if (len == 0) {
            return false;
        }
        for (int i = 1; i < len; ++i) {
            buf[i - 1] = buf[i];
        }
        --len;
        return true;
    }

    // insert_after position "before index k" means inserting after element k,
    // but for forward_list we often work with "prev" positions.
    // We'll define:
    //  - before_begin corresponds to prev_index = -1
    //  - inserting after prev_index inserts at index prev_index+1
    void insert_after(const int prev_index, const int v) {
        const int ins = prev_index + 1;
        assert(ins >= 0 && ins <= len, "model insert_after bad index");
        assert(len < MAX, "model overflow");
        for (int i = len; i > ins; --i) {
            buf[i] = buf[i - 1];
        }
        buf[ins] = v;
        ++len;
    }

    // erase_after(prev_index): erase element at index prev_index+1
    // returns next index (prev_index+1 after erase) as the iterator target
    int erase_after(const int prev_index) {
        const int del = prev_index + 1;
        if (del < 0 || del >= len) {
            return -1; // means end/null
        }
        for (int i = del + 1; i < len; ++i) {
            buf[i - 1] = buf[i];
        }
        --len;
        // return "iterator to element after erased" => del is that index now
        if (del >= len) {
            return -1;
        }
        return del;
    }

    // erase_after(prev_index, stop_index): remove [prev_index+1, stop_index)
    // stop_index is an iterator index into buf (0..len), with len meaning end.
    int erase_after_range(const int prev_index, int stop_index) {
        int start = prev_index + 1;
        if (start < 0) {
            start = 0;
        }
        if (stop_index < 0) {
            stop_index = 0;
        }
        if (stop_index > len) {
            stop_index = len;
        }
        if (start > len) {
            start = len;
        }
        if (start >= stop_index) {
            return stop_index == len ? -1 : stop_index;
        }

        const int remove_n = stop_index - start;
        for (int i = stop_index; i < len; ++i) {
            buf[i - remove_n] = buf[i];
        }
        len -= remove_n;

        if (start >= len) {
            return -1;
        }
        return start;
    }

    uint64 remove_value(const int v) {
        uint64 removed = 0;
        int w = 0;
        for (int r = 0; r < len; ++r) {
            if (buf[r] == v) {
                ++removed;
            } else {
                buf[w++] = buf[r];
            }
        }
        len = w;
        return removed;
    }

    template <class Pred> uint64 remove_if(Pred pred) {
        uint64 removed = 0;
        int w = 0;
        for (int r = 0; r < len; ++r) {
            if (pred(buf[r])) {
                ++removed;
            } else {
                buf[w++] = buf[r];
            }
        }
        len = w;
        return removed;
    }

    void reverse() {
        for (int i = 0; i < len / 2; ++i) {
            const int j = len - 1 - i;
            const int tmp = buf[i];
            buf[i] = buf[j];
            buf[j] = tmp;
        }
    }
};

// ----------------- invariants / comparison -----------------

// Detect cycles (Floyd), count nodes, and optionally validate against model.
static int list_count_and_cycle_check(stl::ts_forward_list<int> &lst) {
    // Must traverse under lock to avoid racing with writers.
    auto view = lst.locked();

    // cycle detection
    auto slow = view.begin();
    auto fast = view.begin();

    bool fast_started = false;
    while (fast != view.end()) {
        ++fast; // 1 step
        if (fast == view.end()) {
            break;
        }
        ++fast; // 2 steps
        ++slow; // 1 step

        // if both valid and equal => cycle
        if (slow == fast) {
            panic("ts_forward_list selftest: cycle detected");
        }
        fast_started = true;
        (void)fast_started;
    }

    int cnt = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        ++cnt;
    }
    return cnt;
}

static void
require_equals_model(stl::ts_forward_list<int> &lst, const model_list &m) {
    // traverse under lock
    auto view = lst.locked();

    int i = 0;
    for (const int &it : view) {
        assert(i < m.len, "ts_forward_list selftest: list longer than model");
        if (it != m.buf[i]) {
            panic("ts_forward_list selftest: element mismatch");
        }
        ++i;
    }
    if (i != m.len) {
        panic("ts_forward_list selftest: list shorter than model");
    }
}

// ----------------- deterministic "basic" tests -----------------

static void test_basics() {
    using stl::ts_forward_list;

    ts_forward_list<int> lst;

    assert(lst.empty(), "basic: empty() should be true initially");
    assert(list_count_and_cycle_check(lst) == 0, "basic: count should be 0");

    // push_front order
    lst.push_front(1);
    lst.push_front(2);
    lst.push_front(3);
    assert(!lst.empty(), "basic: empty() false after pushes");
    assert(lst.front() == 3, "basic: front mismatch after pushes");

    {
        auto view = lst.locked();
        auto it = view.begin();
        assert(it != view.end(), "basic: begin should not be end");
        assert(*it == 3, "basic: iter[0] != 3");
        ++it;
        assert(*it == 2, "basic: iter[1] != 2");
        ++it;
        assert(*it == 1, "basic: iter[2] != 1");
        ++it;
        assert(it == view.end(), "basic: should end after 3 elems");
    }

    // pop_front
    assert(lst.pop_front(), "basic: pop_front should succeed");
    assert(lst.front() == 2, "basic: front mismatch after pop");
    assert(lst.pop_front(), "basic: pop_front should succeed2");
    assert(lst.front() == 1, "basic: front mismatch after pop2");
    assert(lst.pop_front(), "basic: pop_front should succeed3");
    assert(lst.empty(), "basic: should be empty after pops");
    assert(!lst.pop_front(), "basic: pop_front on empty should be false");

    // insert_after / erase_after single
    // Build: [10, 20, 30]
    lst.push_front(30);
    lst.push_front(20);
    lst.push_front(10);

    const auto bb = lst.before_begin();
    lst.insert_after(bb, 11); // [11,10,20,30]

    {
        static model_list m;
        m.clear();
        m.push_front(30);
        m.push_front(20);
        m.push_front(10); // [10, 20, 30]
        // this insert 11 at index 0, making the list [11,10,20,30]
        m.insert_after(-1, 11);
        require_equals_model(lst, m);
    }

    // erase_after(before_begin) removes head (11)
    lst.erase_after(lst.before_begin()); // removes 11 => [10,20,30]
    {
        static model_list m;
        m.clear();
        m.push_front(30);
        m.push_front(20);
        m.push_front(10);
        require_equals_model(lst, m);
    }

    // We'll test range erase using before_begin (publicly accessible) instead:
    // erase_after(before_begin, end) should clear list.
    lst.erase_after(lst.before_begin(), lst.end());
    assert(lst.empty(), "basic: erase_after(before_begin,end) should clear");

    // remove / remove_if / reverse / clear
    // Build: [1,2,3,2,4,2]
    lst.push_front(2);
    lst.push_front(4);
    lst.push_front(2);
    lst.push_front(3);
    lst.push_front(2);
    lst.push_front(1);

    assert(lst.remove(2) == 3, "basic: remove(2) should remove 3 elems");
    {
        static model_list m;
        m.clear();
        m.push_front(2);
        m.push_front(4);
        m.push_front(2);
        m.push_front(3);
        m.push_front(2);
        m.push_front(1);
        m.remove_value(2);
        require_equals_model(lst, m);
    }

    // remove_if even numbers (removes 4)
    assert(lst.remove_if([](const int x) { return x % 2 == 0; }) == 1,
           "basic: remove_if(even) should remove 1");
    {
        static model_list m;
        m.clear();
        m.push_front(4);
        m.push_front(3);
        m.push_front(1);
        m.remove_if([](const int x) { return x % 2 == 0; });
        require_equals_model(lst, m);
    }

    // reverse [1,3] -> [3,1]
    lst.reverse();
    {
        static model_list m;
        m.clear();
        m.push_front(1);
        m.push_front(3);
        require_equals_model(lst, m);
    }

    lst.clear();
    assert(lst.empty(), "basic: clear should empty");
    assert(list_count_and_cycle_check(lst) == 0,
           "basic: count should be 0 after clear");
}

// ----------------- randomized single-thread test -----------------

static void test_random_single_thread() {
    using stl::ts_forward_list;

    ts_forward_list<int> lst;
    static model_list m;
    m.clear();

    uint32 seed = 0x12345678u;

    // perform many ops and compare after each batch
    constexpr int OPS = 20000;

    for (int step = 0; step < OPS; ++step) {
        const uint32 r = xor_shift32(seed);
        const int op = static_cast<int>(r % 10);

        // keep values small to create duplicates for remove/remove_if
        int val = static_cast<int>(xor_shift32(seed) % 17 - 8); // [-8..8]

        switch (op) {
        case 0: // push_front
        case 1: {
            lst.push_front(val);
            m.push_front(val);
            break;
        }
        case 2: // pop_front
        {
            const bool a = lst.pop_front();
            const bool b = m.pop_front();
            assert(a == b, "random: pop_front return mismatch");
            break;
        }
        case 3: // clear
        {
            lst.clear();
            m.clear();
            break;
        }
        case 4: // reverse
        {
            lst.reverse();
            m.reverse();
            break;
        }
        case 5: // remove(value)
        {
            const uint64 ra = lst.remove(val);
            const uint64 rb = m.remove_value(val);
            assert(ra == rb, "random: remove count mismatch");
            break;
        }
        case 6: // remove_if (x < 0)
        {
            const uint64 ra = lst.remove_if([](const int x) { return x < 0; });
            const uint64 rb = m.remove_if([](const int x) { return x < 0; });
            assert(ra == rb, "random: remove_if count mismatch");
            break;
        }
        case 7: // insert_after(before_begin)  (this is a key API)
        {
            const auto bb = lst.before_begin();
            lst.insert_after(bb, val); // inserts at front+1 in list terms
            m.insert_after(-1, val);
            break;
        }
        case 8: // erase_after(before_begin)
        {
            const auto bb = lst.before_begin();
            auto it = lst.erase_after(bb);
            // model: erase after -1 => erase index 0
            const int nexti = m.erase_after(-1);

            // If model says end, iterator should be ended
            const bool model_end = nexti == -1;
            assert((it == lst.end()) == model_end,
                   "random: erase_after return iterator mismatch");
            break;
        }
        case 9: // erase_after(before_begin, end) sometimes (range erase)
        {
            // 1/4 of the time clear via range erase; otherwise erase up to some
            // stop
            if ((xor_shift32(seed) & 3u) == 0) {
                lst.erase_after(lst.before_begin(), lst.end());
                m.clear();
            } else {
                // choose a stop index in model and erase range [0, stop)
                const int stop =
                    m.len == 0
                        ? 0
                        : static_cast<int>(xor_shift32(seed) %
                                           static_cast<uint32>(m.len + 1));
                // For list, easiest is: build "last" iterator by walking stop
                // steps under lock
                ts_forward_list<int>::iterator last;
                {
                    auto view = lst.locked();
                    auto it = view.begin();
                    int k = 0;
                    while (k < stop && it != view.end()) {
                        ++it;
                        ++k;
                    }
                    last = it; // copy iterator
                }
                lst.erase_after(lst.before_begin(), last);
                m.erase_after_range(-1, stop);
            }
            break;
        }
        default:;
        }

        // validate often
        if (step % 97 == 0) {
            const int cnt = list_count_and_cycle_check(lst);
            assert(cnt == m.len, "random: count mismatch");
            require_equals_model(lst, m);
        }
    }

    // final full check
    const int cnt = list_count_and_cycle_check(lst);
    assert(cnt == m.len, "random: final count mismatch");
    require_equals_model(lst, m);
}

void ts_forward_list_self_test() {
    test_basics();
    test_random_single_thread();
}

} // namespace xv6::test