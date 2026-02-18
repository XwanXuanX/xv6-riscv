#pragma once

// compile with -DNDEBUG to disable asserts in release mode
#ifdef NDEBUG
#define assert(expr) ((void)0)
#define assert_msg(expr, msg) ((void)0)
#else

static __attribute__((noreturn)) void
assert_fail(const char *expr, const char *file, int line, const char *func,
            const char *msg) {
    (void)func;
    (void)msg;
    xv6::panic("assertion failed");
}

// PUBLIC MACROS:
// ReSharper disable once CppInconsistentNaming
#define assert0(expr)                                                          \
    ((expr) ? (void)0 : assert_fail(#expr, __FILE__, __LINE__, __func__, 0))

// ReSharper disable once CppInconsistentNaming
#define assert(expr, msg)                                                      \
    ((expr) ? (void)0 : assert_fail(#expr, __FILE__, __LINE__, __func__, (msg)))

#endif // NDEBUG
