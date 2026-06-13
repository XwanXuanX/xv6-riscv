// A thread-safe singly linked list
#pragma once

#include "kernel/lib/types.h"
#include "kernel/sync/spinlock.h"
#include "kernel/mm/slab_allocator.h"
#include "kernel/util/lock_guard.h"
#include "kernel/util/do_not_copy.h"

namespace xv6::test {
void ts_forward_list_self_test();
}

namespace xv6::stl {

template <typename T> class ts_forward_list : util::do_not_copy {
    struct node {
        node *next;
        T value;

        template <class... Args>
        explicit node(node *n, Args &&...args)
            : next(n), value(std::forward<Args>(args)...) {}
    };

    struct sentinel_node {
        node *next;
    };

  public:
    class iterator {
      public:
        using value_type = T;
        using reference = T &;
        using pointer = T *;

        iterator() : n_(nullptr) {}
        explicit iterator(node *n) : n_(n) {}

        reference operator*() const { return n_->value; }
        pointer operator->() const { return &n_->value; }

        iterator &operator++() {
            n_ = n_->next;
            return *this;
        }
        iterator operator++(int) {
            iterator tmp(*this);
            ++*this;
            return tmp;
        }

        friend bool operator==(const iterator &a, const iterator &b) {
            return a.n_ == b.n_;
        }
        friend bool operator!=(const iterator &a, const iterator &b) {
            return a.n_ != b.n_;
        }

      private:
        friend class ts_forward_list;
        node *n_;
    };

    class const_iterator {
      public:
        using value_type = const T;
        using reference = const T &;
        using pointer = const T *;

        const_iterator() : n_(nullptr) {}
        explicit const_iterator(const node *n) : n_(n) {}
        explicit const_iterator(iterator it) : n_(it.n_) {}

        reference operator*() const { return n_->value; }
        pointer operator->() const { return &n_->value; }

        const_iterator &operator++() {
            n_ = n_->next;
            return *this;
        }
        const_iterator operator++(int) {
            const_iterator tmp(*this);
            ++*this;
            return tmp;
        }

        friend bool
        operator==(const const_iterator &a, const const_iterator &b) {
            return a.n_ == b.n_;
        }
        friend bool
        operator!=(const const_iterator &a, const const_iterator &b) {
            return a.n_ != b.n_;
        }

      private:
        friend class ts_forward_list;
        const node *n_;
    };

    // Represents the position *before* some element (like
    // std::forward_list::before_begin())
    class before_iterator {
      public:
        before_iterator() : prev_(nullptr), is_sentinel_(true) {}

      private:
        friend class ts_forward_list;
        explicit before_iterator(node *prev)
            : prev_(prev), is_sentinel_(false) {}
        explicit before_iterator(sentinel_node *s)
            : prev_(reinterpret_cast<node *>(s)), is_sentinel_(true) {}
        node *prev_;
        bool is_sentinel_;
    };

    // Lock-held view for safe traversal
    class locked_view {
      public:
        explicit locked_view(ts_forward_list &lst) : lst_(lst), g_(lst_.lk_) {}
        iterator begin() { return iterator(lst_.before_.next); }
        iterator end() { return iterator(nullptr); }
        before_iterator before_begin() {
            return before_iterator(&lst_.before_);
        }

      private:
        ts_forward_list &lst_;
        util::lock_guard<spinlock> g_;
    };

    ts_forward_list() {
        lk_.init_lock("list_lock");
        before_.next = nullptr;
    }
    ~ts_forward_list() { clear(); }

    bool empty() const {
        util::lock_guard g(lk_);
        return before_.next == nullptr;
    }

    // Same contract as std::forward_list: UB if empty.
    T &front() {
        util::lock_guard g(lk_);
        return before_.next->value;
    }
    const T &front() const {
        util::lock_guard g(lk_);
        return before_.next->value;
    }

    // Iterators returned here are NOT safe under concurrent writers.
    iterator begin() {
        util::lock_guard g(lk_);
        return iterator(before_.next);
    }
    iterator end() { return iterator(nullptr); }

    const_iterator begin() const {
        util::lock_guard g(lk_);
        return const_iterator(before_.next);
    }
    const_iterator end() const { return const_iterator(nullptr); }

    before_iterator before_begin() {
        util::lock_guard g(lk_);
        return before_iterator(&before_);
    }

    locked_view locked() { return locked_view(*this); }

    template <class... Args> void push_front(Args &&...args) {
        util::lock_guard g(lk_);
        node *n = new_node(before_.next, std::forward<Args>(args)...);
        before_.next = n;
    }

    bool pop_front() {
        util::lock_guard g(lk_);
        node *h = before_.next;
        if (!h) {
            return false;
        }
        before_.next = h->next;
        delete_node(h);
        return true;
    }

    bool pop_front_value(T &out) {
        util::lock_guard g(lk_);
        node *h = before_.next;
        if (!h) {
            return false;
        }
        out = h->value;
        before_.next = h->next;
        delete_node(h);
        return true;
    }

    void clear() {
        util::lock_guard g(lk_);
        node *cur = before_.next;
        before_.next = nullptr;
        while (cur) {
            node *nxt = cur->next;
            delete_node(cur);
            cur = nxt;
        }
    }

    template <class... Args>
    iterator insert_after(const before_iterator pos, Args &&...args) {
        util::lock_guard g(lk_);
        node *prev = resolve_prev_unchecked(pos);
        node *n = new_node(prev->next, std::forward<Args>(args)...);
        prev->next = n;
        return iterator(n);
    }

    iterator erase_after(const before_iterator pos) {
        util::lock_guard g(lk_);
        node *prev = resolve_prev_unchecked(pos);
        node *victim = prev->next;
        if (!victim) {
            return iterator(nullptr);
        }
        prev->next = victim->next;
        node *ret = prev->next;
        delete_node(victim);
        return iterator(ret);
    }

    iterator erase_after(const before_iterator first, iterator last) {
        util::lock_guard g(lk_);
        node *prev = resolve_prev_unchecked(first);
        node *cur = prev->next;
        node *stop = last.n_;
        while (cur && cur != stop) {
            node *nxt = cur->next;
            delete_node(cur);
            cur = nxt;
        }
        prev->next = stop;
        return iterator(stop);
    }

    uint64 remove(const T &value) {
        return remove_if([&](const T &x) { return x == value; });
    }

    template <class Pred> uint64 remove_if(Pred pred) {
        util::lock_guard g(lk_);
        uint64 removed = 0;
        node *prev = reinterpret_cast<node *>(&before_);
        node *cur = before_.next;

        while (cur) {
            if (pred(cur->value)) {
                node *victim = cur;
                cur = cur->next;
                prev->next = cur;
                delete_node(victim);
                ++removed;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }
        return removed;
    }

    void reverse() {
        util::lock_guard g(lk_);
        node *prev = nullptr;
        node *cur = before_.next;
        while (cur) {
            node *nxt = cur->next;
            cur->next = prev;
            prev = cur;
            cur = nxt;
        }
        before_.next = prev;
    }

  private:
    template <class... Args> static node *new_node(node *next, Args &&...args) {
        node *mem = slab_alloc_t<node>();
        // If your slab can return nullptr, decide your kernel policy:
        // - panic("OOM") or return nullptr and handle it.
        return new (mem) node(next, std::forward<Args>(args)...);
    }

    static void delete_node(node *n) {
        n->~node();
        slab_free_t<node>(n);
    }

    node *resolve_prev_unchecked(before_iterator pos) {
        if (pos.is_sentinel_) {
            return reinterpret_cast<node *>(&before_);
        }
        return pos.prev_;
    }

    mutable spinlock lk_{};
    sentinel_node before_;
};

} // namespace xv6::stl
