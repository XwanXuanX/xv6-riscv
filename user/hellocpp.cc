/**
 * This is a simple hello world program that is written in C++
 * to test the compatibility of the C++ lang and the kernel.
 * 
 * It is also a playground for many C++ features.
 * More C++ runtime will be supported.
 */

#include "kernel/types.h"
#include "user/user.h" // Includes a bunch of system calls declarations

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

int main(void) {
    HelloWorld hw;
    hw.hi();
    exit(0);
}
