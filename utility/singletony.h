#pragma once

#include "do_not_copy.h"
#include "do_not_move.h"
#include <type_traits>

namespace xv6::util {

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
        static T inst;
        return inst;
    }

  protected:
    constexpr singleton() = default;
    ~singleton() = default;

  private:
    friend T;
};

} // namespace xv6::util