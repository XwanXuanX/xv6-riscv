// A thread-safe doubly linked list
#pragma once

#include "kernel/lib/types.h"
#include "kernel/sync/spinlock.h"
#include "kernel/mm/slab.h"
#include "kernel/util/lock_guard.h"
#include "kernel/util/do_not_copy.h"

namespace xv6::test {
void ts_list_self_test();
}

namespace xv6::stl {

template <typename T> class ts_list : util::do_not_copy {
    struct node {
        node *prev;
        node *next;
        T value;

        template <class... Args>
        explicit node(node *p, node *n, Args &&...args)
            : prev(p), next(n), value(std::forward<Args>(args)...) {}
    };

    struct sentinel_node {
        node *prev;
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

        iterator &operator--() {
            n_ = n_->prev;
            return *this;
        }
        iterator operator--(int) {
            iterator tmp(*this);
            --*this;
            return tmp;
        }

        friend bool operator==(const iterator &a, const iterator &b) {
            return a.n_ == b.n_;
        }
        friend bool operator!=(const iterator &a, const iterator &b) {
            return a.n_ != b.n_;
        }

      private:
        friend class ts_list;
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

        const_iterator &operator--() {
            n_ = n_->prev;
            return *this;
        }
        const_iterator operator--(int) {
            const_iterator tmp(*this);
            --*this;
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
        friend class ts_list;
        const node *n_;
    };

    class locked_view {
      public:
        explicit locked_view(ts_list &lst) : lst_(lst), g_(lst_.lk_) {}

        iterator begin() { return iterator(lst_.head()); }
        iterator end() { return iterator(lst_.sentinel_as_node()); }

      private:
        ts_list &lst_;
        util::lock_guard<spinlock> g_;
    };

    ts_list() {
        lk_.init_lock("list_lock");
        before_.prev = sentinel_as_node();
        before_.next = sentinel_as_node();
        size_ = 0;
    }

    ~ts_list() { clear(); }

    [[nodiscard]] bool empty() const {
        util::lock_guard g(lk_);
        return size_ == 0;
    }

    [[nodiscard]] uint64 size() const {
        util::lock_guard g(lk_);
        return size_;
    }

    T &front() {
        util::lock_guard g(lk_);
        return head()->value;
    }

    const T &front() const {
        util::lock_guard g(lk_);
        return head()->value;
    }

    T &back() {
        util::lock_guard g(lk_);
        return tail()->value;
    }

    const T &back() const {
        util::lock_guard g(lk_);
        return tail()->value;
    }

    // Iterators returned here are NOT safe under concurrent writers.
    iterator begin() {
        util::lock_guard g(lk_);
        return iterator(head());
    }

    iterator end() { return iterator(sentinel_as_node()); }

    const_iterator begin() const {
        util::lock_guard g(lk_);
        return const_iterator(head());
    }

    const_iterator end() const { return const_iterator(sentinel_as_node()); }

    locked_view locked() { return locked_view(*this); }

    template <class... Args> void push_front(Args &&...args) {
        util::lock_guard g(lk_);
        insert_between(sentinel_as_node(), head(), std::forward<Args>(args)...);
    }

    template <class... Args> void push_back(Args &&...args) {
        util::lock_guard g(lk_);
        insert_between(tail(), sentinel_as_node(), std::forward<Args>(args)...);
    }

    bool pop_front() {
        util::lock_guard g(lk_);
        if (empty_unlocked()) {
            return false;
        }
        unlink_and_delete(head());
        return true;
    }

    bool pop_back() {
        util::lock_guard g(lk_);
        if (empty_unlocked()) {
            return false;
        }
        unlink_and_delete(tail());
        return true;
    }

    bool pop_front_value(T &out) {
        util::lock_guard g(lk_);
        if (empty_unlocked()) {
            return false;
        }
        node *h = head();
        out = h->value;
        unlink_and_delete(h);
        return true;
    }

    bool pop_back_value(T &out) {
        util::lock_guard g(lk_);
        if (empty_unlocked()) {
            return false;
        }
        node *t = tail();
        out = t->value;
        unlink_and_delete(t);
        return true;
    }

    template <class... Args> iterator insert(iterator pos, Args &&...args) {
        util::lock_guard g(lk_);
        node *next = pos.n_;
        node *prev = next->prev;
        return iterator(
            insert_between(prev, next, std::forward<Args>(args)...));
    }

    iterator erase(iterator pos) {
        util::lock_guard g(lk_);
        node *victim = pos.n_;
        if (victim == sentinel_as_node()) {
            return end();
        }
        node *ret = victim->next;
        unlink_and_delete(victim);
        return iterator(ret);
    }

    iterator erase(iterator first, iterator last) {
        util::lock_guard g(lk_);
        node *cur = first.n_;
        node *stop = last.n_;
        while (cur != sentinel_as_node() && cur != stop) {
            node *nxt = cur->next;
            unlink_and_delete(cur);
            cur = nxt;
        }
        return iterator(stop);
    }

    uint64 remove(const T &value) {
        return remove_if([&](const T &x) { return x == value; });
    }

    template <class Pred> uint64 remove_if(Pred pred) {
        util::lock_guard g(lk_);
        uint64 removed = 0;
        node *cur = head();

        while (cur != sentinel_as_node()) {
            node *nxt = cur->next;
            if (pred(cur->value)) {
                unlink_and_delete(cur);
                ++removed;
            }
            cur = nxt;
        }
        return removed;
    }

    bool erase_first_value(const T &value) {
        util::lock_guard g(lk_);
        node *cur = head();
        while (cur != sentinel_as_node()) {
            if (cur->value == value) {
                unlink_and_delete(cur);
                return true;
            }
            cur = cur->next;
        }
        return false;
    }

    void clear() {
        util::lock_guard g(lk_);
        node *cur = head();
        while (cur != sentinel_as_node()) {
            node *nxt = cur->next;
            delete_node(cur);
            cur = nxt;
        }
        before_.prev = sentinel_as_node();
        before_.next = sentinel_as_node();
        size_ = 0;
    }

    void reverse() {
        util::lock_guard g(lk_);
        node *cur = sentinel_as_node();
        do {
            node *tmp = cur->next;
            cur->next = cur->prev;
            cur->prev = tmp;
            cur = tmp;
        } while (cur != sentinel_as_node());
    }

    // Run a function with the list lock held.
    // Useful for composing multistep operations atomically (e.g., lock-ordering
    // with other locks, publishing objects without races).
    template <class Fn> decltype(auto) with_lock(Fn &&fn) {
        util::lock_guard g(lk_);
        return fn();
    }

    template <class Fn> decltype(auto) with_lock(Fn &&fn) const {
        util::lock_guard g(lk_);
        return fn();
    }

    // Unlocked variants: caller MUST already hold the list lock (via
    // with_lock()).
    template <class... Args> void push_front_unlocked(Args &&...args) {
        insert_between(sentinel_as_node(), head(), std::forward<Args>(args)...);
    }

    template <class... Args> void push_back_unlocked(Args &&...args) {
        insert_between(tail(), sentinel_as_node(), std::forward<Args>(args)...);
    }

    bool pop_front_unlocked() {
        if (empty_unlocked()) {
            return false;
        }
        unlink_and_delete(head());
        return true;
    }

    bool pop_back_unlocked() {
        if (empty_unlocked()) {
            return false;
        }
        unlink_and_delete(tail());
        return true;
    }

    bool pop_front_value_unlocked(T &out) {
        if (empty_unlocked()) {
            return false;
        }
        node *h = head();
        out = h->value;
        unlink_and_delete(h);
        return true;
    }

    bool pop_back_value_unlocked(T &out) {
        if (empty_unlocked()) {
            return false;
        }
        node *t = tail();
        out = t->value;
        unlink_and_delete(t);
        return true;
    }

    template <class... Args>
    iterator insert_unlocked(iterator pos, Args &&...args) {
        node *next = pos.n_;
        node *prev = next->prev;
        return iterator(
            insert_between(prev, next, std::forward<Args>(args)...));
    }

    iterator erase_unlocked(iterator pos) {
        node *victim = pos.n_;
        if (victim == sentinel_as_node()) {
            return iterator(sentinel_as_node());
        }
        node *ret = victim->next;
        unlink_and_delete(victim);
        return iterator(ret);
    }

    iterator erase_unlocked(iterator first, iterator last) {
        node *cur = first.n_;
        node *stop = last.n_;
        while (cur != sentinel_as_node() && cur != stop) {
            node *nxt = cur->next;
            unlink_and_delete(cur);
            cur = nxt;
        }
        return iterator(stop);
    }

    bool erase_first_value_unlocked(const T &value) {
        node *cur = head();
        while (cur != sentinel_as_node()) {
            if (cur->value == value) {
                unlink_and_delete(cur);
                return true;
            }
            cur = cur->next;
        }
        return false;
    }

    void clear_unlocked() {
        node *cur = head();
        while (cur != sentinel_as_node()) {
            node *nxt = cur->next;
            delete_node(cur);
            cur = nxt;
        }
        before_.prev = sentinel_as_node();
        before_.next = sentinel_as_node();
        size_ = 0;
    }

    [[nodiscard]] bool holding() const { return lk_.holding(); }

  private:
    node *sentinel_as_node() const {
        return reinterpret_cast<node *>(const_cast<sentinel_node *>(&before_));
    }

    node *head() const { return before_.next; }
    node *tail() const { return before_.prev; }

    bool empty_unlocked() const { return size_ == 0; }

    template <class... Args>
    static node *new_node(node *prev, node *next, Args &&...args) {
        node *mem = slab_alloc_t<node>();
        return new (mem) node(prev, next, std::forward<Args>(args)...);
    }

    static void delete_node(node *n) {
        n->~node();
        slab_free_t<node>(n);
    }

    template <class... Args>
    node *insert_between(node *prev, node *next, Args &&...args) {
        node *n = new_node(prev, next, std::forward<Args>(args)...);
        prev->next = n;
        next->prev = n;
        ++size_;
        return n;
    }

    static void unlink(node *n) {
        n->prev->next = n->next;
        n->next->prev = n->prev;
    }

    void unlink_and_delete(node *n) {
        unlink(n);
        delete_node(n);
        --size_;
    }

    mutable spinlock lk_{};
    sentinel_node before_;
    uint64 size_ = 0;
};

} // namespace xv6::stl