// This is a kernel self test at boot time

#include "kernel/defs.h"
#include "kernel/stl/ts_list.h"
#include "kernel/util/assert.h"

#include <array>

namespace xv6::test {

static uint32 xor_shift32(uint32 &x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

struct model_list {
    static constexpr int MAX = 2048;
    std::array<int, MAX> buf{};
    int len = 0;

    void clear() { len = 0; }

    [[nodiscard]] bool empty() const { return len == 0; }

    [[nodiscard]] int front() const {
        assert(len > 0, "ts_list selftest model: front on empty");
        return buf[0];
    }

    [[nodiscard]] int back() const {
        assert(len > 0, "ts_list selftest model: back on empty");
        return buf[len - 1];
    }

    void push_front(const int v) {
        assert(len < MAX, "ts_list selftest model: overflow");
        for (int i = len; i > 0; --i) {
            buf[i] = buf[i - 1];
        }
        buf[0] = v;
        ++len;
    }

    void push_back(const int v) {
        assert(len < MAX, "ts_list selftest model: overflow");
        buf[len++] = v;
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

    bool pop_back() {
        if (len == 0) {
            return false;
        }
        --len;
        return true;
    }

    bool pop_front_value(int &out) {
        if (len == 0) {
            return false;
        }
        out = buf[0];
        return pop_front();
    }

    bool pop_back_value(int &out) {
        if (len == 0) {
            return false;
        }
        out = buf[len - 1];
        return pop_back();
    }

    void insert(const int pos, const int v) {
        assert(pos >= 0 && pos <= len,
               "ts_list selftest model: bad insert pos");
        assert(len < MAX, "ts_list selftest model: overflow");
        for (int i = len; i > pos; --i) {
            buf[i] = buf[i - 1];
        }
        buf[pos] = v;
        ++len;
    }

    // erase one element at pos, return next position
    int erase(const int pos) {
        assert(pos >= 0 && pos <= len, "ts_list selftest model: bad erase pos");
        if (pos == len) {
            return len; // erase(end()) => no-op in model usage
        }
        for (int i = pos + 1; i < len; ++i) {
            buf[i - 1] = buf[i];
        }
        --len;
        return pos;
    }

    // erase [first,last), return last survivor position
    int erase(const int first, const int last) {
        assert(first >= 0 && first <= len,
               "ts_list selftest model: bad erase first");
        assert(last >= 0 && last <= len,
               "ts_list selftest model: bad erase last");
        assert(first <= last, "ts_list selftest model: erase first > last");
        const int count = last - first;
        for (int i = last; i < len; ++i) {
            buf[i - count] = buf[i];
        }
        len -= count;
        return first;
    }

    uint64 remove_value(const int value) {
        uint64 removed = 0;
        int w = 0;
        for (int r = 0; r < len; ++r) {
            if (buf[r] == value) {
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
        for (int i = 0, j = len - 1; i < j; ++i, --j) {
            const int tmp = buf[i];
            buf[i] = buf[j];
            buf[j] = tmp;
        }
    }
};

static void require_equals_model(stl::ts_list<int> &lst, const model_list &m) {
    {
        auto view = lst.locked();
        int i = 0;
        for (const int &x : view) {
            assert(i < m.len, "ts_list selftest: list longer than model");
            if (x != m.buf[i]) {
                panic("ts_list selftest: element mismatch");
            }
            ++i;
        }
        if (i != m.len) {
            panic("ts_list selftest: list shorter than model");
        }
    }

    // Also verify backward traversal using -- from end().
    {
        auto view = lst.locked();
        auto it = view.end();
        int i = m.len - 1;
        while (i >= 0) {
            --it;
            if (*it != m.buf[i]) {
                panic("ts_list selftest: reverse iterator mismatch");
            }
            --i;
        }
        if (m.len == 0) {
            assert(view.begin() == view.end(),
                   "ts_list selftest: empty begin/end mismatch");
        }
    }
}

static int list_count_and_cycle_check(stl::ts_list<int> &lst) {
    auto view = lst.locked();

    // forward count
    int forward = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        ++forward;
        if (forward > 100000) {
            panic("ts_list selftest: probable cycle forward");
        }
    }

    // backward count
    int backward = 0;
    auto it = view.end();
    while (it != view.begin()) {
        --it;
        ++backward;
        if (backward > 100000) {
            panic("ts_list selftest: probable cycle backward");
        }
    }

    if (forward != backward) {
        panic("ts_list selftest: forward/backward count mismatch");
    }
    return forward;
}

// Build iterator to index pos (0...len), where len means end()
static stl::ts_list<int>::iterator
nth_iter(stl::ts_list<int> &lst, const int pos) {
    auto view = lst.locked();
    auto it = view.begin();
    int i = 0;
    while (i < pos && it != view.end()) {
        ++it;
        ++i;
    }
    return it;
}

static void test_basics() {
    using stl::ts_list;

    ts_list<int> lst;

    assert(lst.empty(), "ts_list basic: empty initially");
    assert(list_count_and_cycle_check(lst) == 0,
           "ts_list basic: initial count not zero");

    // push_front / push_back
    lst.push_back(2);  // [2]
    lst.push_front(1); // [1,2]
    lst.push_back(3);  // [1,2,3]

    assert(!lst.empty(), "ts_list basic: should not be empty");
    assert(lst.front() == 1, "ts_list basic: front mismatch");
    assert(lst.back() == 3, "ts_list basic: back mismatch");

    {
        auto view = lst.locked();
        auto it = view.begin();
        assert(*it == 1, "ts_list basic: iter[0]");
        ++it;
        assert(*it == 2, "ts_list basic: iter[1]");
        ++it;
        assert(*it == 3, "ts_list basic: iter[2]");
        ++it;
        assert(it == view.end(), "ts_list basic: end mismatch");

        --it;
        assert(*it == 3, "ts_list basic: reverse iter[2]");
        --it;
        assert(*it == 2, "ts_list basic: reverse iter[1]");
        --it;
        assert(*it == 1, "ts_list basic: reverse iter[0]");
    }

    // pop_front / pop_back
    assert(lst.pop_front(), "ts_list basic: pop_front failed"); // [2,3]
    assert(lst.front() == 2, "ts_list basic: front after pop_front");
    assert(lst.pop_back(), "ts_list basic: pop_back failed"); // [2]
    assert(lst.back() == 2, "ts_list basic: back after pop_back");
    assert(lst.pop_front(), "ts_list basic: pop_front single failed");
    assert(lst.empty(), "ts_list basic: should be empty after pops");
    assert(!lst.pop_front(), "ts_list basic: pop_front on empty");
    assert(!lst.pop_back(), "ts_list basic: pop_back on empty");

    // pop value variants
    lst.push_back(10);
    lst.push_back(20);
    lst.push_back(30); // [10,20,30]

    int out = 0;
    assert(lst.pop_front_value(out), "ts_list basic: pop_front_value failed");
    assert(out == 10, "ts_list basic: pop_front_value wrong value");
    assert(lst.pop_back_value(out), "ts_list basic: pop_back_value failed");
    assert(out == 30, "ts_list basic: pop_back_value wrong value");
    assert(lst.pop_back_value(out),
           "ts_list basic: pop_back_value single failed");
    assert(out == 20, "ts_list basic: pop_back_value single wrong");
    assert(lst.empty(), "ts_list basic: empty after pop value variants");

    // insert / erase one
    lst.push_back(10);
    lst.push_back(20);
    lst.push_back(30); // [10,20,30]

    {
        const auto pos = nth_iter(lst, 1); // points to 20
        lst.insert(pos, 15);               // [10,15,20,30]
        static model_list m;
        m.clear();
        m.push_back(10);
        m.push_back(15);
        m.push_back(20);
        m.push_back(30);
        require_equals_model(lst, m);
    }

    {
        const auto pos = nth_iter(lst, 2); // points to 20
        const auto ret = lst.erase(pos);   // [10,15,30], ret->30
        static model_list m;
        m.clear();
        m.push_back(10);
        m.push_back(15);
        m.push_back(30);
        require_equals_model(lst, m);
        assert(ret != lst.end(),
               "ts_list basic: erase return should not be end");
        assert(*ret == 30, "ts_list basic: erase return wrong");
    }

    // erase range
    lst.clear();
    lst.push_back(1);
    lst.push_back(2);
    lst.push_back(3);
    lst.push_back(4);
    lst.push_back(5); // [1,2,3,4,5]

    {
        const auto first = nth_iter(lst, 1); // 2
        const auto last = nth_iter(lst, 4);  // 5
        lst.erase(first, last);              // remove 2,3,4 => [1,5]

        static model_list m;
        m.clear();
        m.push_back(1);
        m.push_back(5);
        require_equals_model(lst, m);
    }

    // remove / remove_if
    lst.clear();
    lst.push_back(1);
    lst.push_back(2);
    lst.push_back(3);
    lst.push_back(2);
    lst.push_back(4);
    lst.push_back(2); // [1,2,3,2,4,2]

    assert(lst.remove(2) == 3, "ts_list basic: remove count wrong");
    {
        static model_list m;
        m.clear();
        m.push_back(1);
        m.push_back(3);
        m.push_back(4);
        require_equals_model(lst, m);
    }

    assert(lst.remove_if([](const int x) { return x % 2 == 0; }) == 1,
           "ts_list basic: remove_if count wrong");
    {
        static model_list m;
        m.clear();
        m.push_back(1);
        m.push_back(3);
        require_equals_model(lst, m);
    }

    // reverse
    lst.reverse();
    {
        static model_list m;
        m.clear();
        m.push_back(3);
        m.push_back(1);
        require_equals_model(lst, m);
    }

    lst.clear();
    assert(lst.empty(), "ts_list basic: clear should empty");
    assert(list_count_and_cycle_check(lst) == 0,
           "ts_list basic: count after clear");
}

static void test_random() {
    using stl::ts_list;

    ts_list<int> lst;
    static model_list m;
    m.clear();

    uint32 seed = 0x13579BDFu;

    constexpr int OPS = 30000;

    for (int step = 0; step < OPS; ++step) {
        const int op = static_cast<int>(xor_shift32(seed) % 12);
        const int val = static_cast<int>(xor_shift32(seed) % 21 - 10);

        switch (op) {
        case 0: // push_front
            lst.push_front(val);
            m.push_front(val);
            break;

        case 1: // push_back
            lst.push_back(val);
            m.push_back(val);
            break;

        case 2: { // pop_front
            const bool a = lst.pop_front();
            const bool b = m.pop_front();
            assert(a == b, "ts_list random: pop_front return mismatch");
            break;
        }

        case 3: { // pop_back
            const bool a = lst.pop_back();
            const bool b = m.pop_back();
            assert(a == b, "ts_list random: pop_back return mismatch");
            break;
        }

        case 4: { // insert at random position
            const int pos =
                m.len == 0 ? 0
                           : static_cast<int>(xor_shift32(seed) %
                                              static_cast<uint32>(m.len + 1));
            const auto it = nth_iter(lst, pos);
            lst.insert(it, val);
            m.insert(pos, val);
            break;
        }

        case 5: { // erase one at random position if non-empty
            if (m.len == 0) {
                break;
            }
            const int pos = static_cast<int>(xor_shift32(seed) %
                                             static_cast<uint32>(m.len));
            const auto it = nth_iter(lst, pos);
            auto ret = lst.erase(it);
            const int next_pos = m.erase(pos);

            auto expected = nth_iter(lst, next_pos);
            assert(ret == expected, "ts_list random: erase return mismatch");
            break;
        }

        case 6: { // erase range
            const int a =
                m.len == 0 ? 0
                           : static_cast<int>(xor_shift32(seed) %
                                              static_cast<uint32>(m.len + 1));
            const int b =
                m.len == 0 ? 0
                           : static_cast<int>(xor_shift32(seed) %
                                              static_cast<uint32>(m.len + 1));
            const int first = a < b ? a : b;
            const int last = a < b ? b : a;

            const auto it1 = nth_iter(lst, first);
            const auto it2 = nth_iter(lst, last);
            auto ret = lst.erase(it1, it2);
            const int next_pos = m.erase(first, last);

            auto expected = nth_iter(lst, next_pos);
            assert(ret == expected,
                   "ts_list random: erase range return mismatch");
            break;
        }

        case 7: { // remove(value)
            const uint64 a = lst.remove(val);
            const uint64 b = m.remove_value(val);
            assert(a == b, "ts_list random: remove count mismatch");
            break;
        }

        case 8: { // remove_if(x<0)
            const uint64 a = lst.remove_if([](const int x) { return x < 0; });
            const uint64 b = m.remove_if([](const int x) { return x < 0; });
            assert(a == b, "ts_list random: remove_if count mismatch");
            break;
        }

        case 9: // reverse
            lst.reverse();
            m.reverse();
            break;

        case 10: // clear
            lst.clear();
            m.clear();
            break;

        case 11: { // front/back sanity if non-empty
            if (!m.empty()) {
                assert(lst.front() == m.front(),
                       "ts_list random: front mismatch");
                assert(lst.back() == m.back(), "ts_list random: back mismatch");
            }
            break;
        }
        default:;
        }

        if (step % 97 == 0) {
            const int cnt = list_count_and_cycle_check(lst);
            assert(cnt == m.len, "ts_list random: count mismatch");
            require_equals_model(lst, m);
        }
    }

    const int cnt = list_count_and_cycle_check(lst);
    assert(cnt == m.len, "ts_list random: final count mismatch");
    require_equals_model(lst, m);
}

void ts_list_self_test() {
    test_basics();
    test_random();
    printf("ts_list selftest: OK\n");
}

} // namespace xv6::test