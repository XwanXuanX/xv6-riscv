#pragma once

#include "do_not_copy.h"
#include "do_not_move.h"
#include <type_traits>

namespace xv6::util {

enum {
    not_started = 0,
    constructing,
    constructed,
};

// How to use:
// class Config : public singleton<Config> {
//     friend class singleton<Config>;
//
// public:
//     void set_value(int v) { value_ = v; }
//     int value() const { return value_; }
//
// private:
//     Config() = default;
//     int value_ = 0;
// };
template <class T> class singleton : do_not_copy, do_not_move {
  public:
    static T &instance() noexcept(std::is_nothrow_default_constructible_v<T>) {
        alignas(T) static unsigned char storage[sizeof(T)];

        static volatile int state = not_started;

        // If already constructed.
        if (state == constructed) {
            // Ensure subsequent reads of *T see initialized contents.
            __sync_synchronize();
            return *reinterpret_cast<T *>(storage);
        }

        // Try to claim initialization by swapping state to 1
        if (__sync_lock_test_and_set(&state, constructing) == 0) {
            // Construct exactly once
            new (storage) T();
            // Publish constructed object before setting state=2
            __sync_synchronize();
            state = constructed;
            return *reinterpret_cast<T *>(storage);
        }

        // Someone else is constructing, wait until fully constructed.
        while (state != 2) {
            asm volatile("nop");
        }

        // Ensure we observe fully initialized object.
        __sync_synchronize();
        return *reinterpret_cast<T *>(storage);
    }

  protected:
    constexpr singleton() = default;
    ~singleton() = default;

  private:
    friend T;
};

} // namespace xv6::util