#pragma once

namespace xv6::util {

// By inheriting this class, you can make the class non-movable
struct do_not_move {
  protected:
    constexpr do_not_move() = default;
    ~do_not_move() = default;

    do_not_move(do_not_move &&) = delete;
    do_not_move &operator=(do_not_move &&) = delete;
};

} // namespace xv6::util