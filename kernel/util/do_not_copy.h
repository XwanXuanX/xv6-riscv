#pragma once

namespace xv6::util {

// By inheriting this class, you can make the class non-copyable
struct do_not_copy {
  protected:
    constexpr do_not_copy() = default;
    ~do_not_copy() = default;

    do_not_copy(const do_not_copy &) = delete;
    do_not_copy &operator=(const do_not_copy &) = delete;
};

} // namespace xv6::util