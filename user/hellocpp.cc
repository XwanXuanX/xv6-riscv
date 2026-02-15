/**
 * This is a simple hello world program that is written in C++
 * to test the compatibility of the C++ lang and the kernel.
 *
 * It is also a playground for many C++ features.
 * More C++ runtime will be supported.
 */

#include "kernel/types.h"
#include "user/user.h" // Includes a bunch of system calls declarations
#include <type_traits>

/**
 * This works:
 *      HelloWorld hw;
 *      hw.hi();
 * Because of 2 things:
 * 1. Zero-overhead class: This class is so simple that doesn't need C++ runtime
 * 2. Static execution: Since the class doesn't make calls to `new` or `std::string` or `std::cout`,
 *                      the linker doesn't even try to link with libstdc++, which obviously doesn't exist
 */
class HelloWorld {
  public:
    static void hi() {
        printf("Hello from C++ xv6!\n");
    }
};

namespace std::details {

enum class S {
    Y,
    N
};

}

namespace std {

inline void assert(const bool cond, const char *msg = nullptr) {
    if (!cond) {
        if (msg != nullptr) {
            printf(msg);
        }
        exit(-1);
    }
}
inline void always_assert(const char *msg = nullptr) {
    assert(false, msg);
}

using nullopt_t = const details::S;
constexpr auto nullopt = details::S::N;

template <typename T>
    requires is_default_constructible_v<T>
struct optional final {
    // Sorry, just gonna copy it for now LMAO
    optional(const T &v) : t(v), s(details::S::Y) {}
    optional(const nullopt_t /* arg unused */) : t(), s(nullopt_t::N) {}

    bool has_value() const noexcept { return s == nullopt_t::Y; }
    // Be careful! This function panics if it doesn't contain a value! So check before call!
    const T &get() const {
        if (!has_value()) {
            always_assert("NO VALUE FROM std::optional!\n");
        }
        return t;
    }

    T t;
    details::S s;
};

class string_view final {
  public:
    explicit string_view(const char *d) : d(d), l(strlen(d)) {}
    const char *data() const { return this->d; }
    uint64 size() const { return this->l; }

  private:
    const char *d;
    const uint64 l;
};

} // namespace std

/**
 * Now I can use all the compile-time power that C++ brings, such as templates!
 * Since templates are resolved at compile-time, they don't need any runtime support.
 *
 * For example, this function checks if two objects are equal by introspecting their types
 */
template <typename T>
std::optional<bool> eq(const T &a, const T &b) {
    if constexpr (std::is_integral_v<T>) {
        return a == b;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return strcmp(a.data(), b.data()) == 0;
    } else {
        return std::nullopt;
    }
}

int main() {
    // Test the eq() function
    {
        const auto r1 = eq(1, 2), r2 = eq(2, 2);
        std::assert(r1.has_value() && r2.has_value());
        printf("r1=%d, r2=%d\n", r1.get(), r2.get());
    }
    {
        std::string_view sv1("Apple"), sv2("Apple"), sv3("Orange");
        const auto r1 = eq(sv1, sv2), r2 = eq(sv1, sv3);
        std::assert(r1.has_value() && r2.has_value());
        printf("r1=%d, r2=%d\n", r1.get(), r2.get());
    }

    exit(0);
}
