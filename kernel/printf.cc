//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "spinlock.h"
#include "defs.h"

namespace xv6 {

volatile int panicking = 0; // printing a panic message
volatile int panicked = 0;  // spinning forever at end of a panic

// lock to avoid interleaving concurrent printf's.
static struct {
    spinlock lock;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(const long long xx, const int base, int sign) {
    char buf[20];
    unsigned long long x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    int i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        consputc(buf[i]);
}

static void
printptr(uint64 x) {
    consputc('0');
    consputc('x');
    for (uint i = 0; i < sizeof(uint64) * 2; i++, x <<= 4)
        consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console.
int printf(const char *fmt, ...) {
    va_list ap;
    int cx, c2;
    const char *s;

    if (panicking == 0)
        pr.lock.lock();

    va_start(ap, fmt);
    for (int i = 0; (cx = fmt[i] & 0xff) != 0; i++) {
        if (cx != '%') {
            consputc(cx);
            continue;
        }
        i++;
        const int c0 = fmt[i + 0] & 0xff;
        int c1 = c2 = 0;
        if (c0)
            c1 = fmt[i + 1] & 0xff;
        if (c1)
            c2 = fmt[i + 2] & 0xff;
        if (c0 == 'd') {
            printint(va_arg(ap, int), 10, 1);
        } else if (c0 == 'l' && c1 == 'd') {
            printint(va_arg(ap, uint64), 10, 1);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
            printint(va_arg(ap, uint64), 10, 1);
            i += 2;
        } else if (c0 == 'u') {
            printint(va_arg(ap, uint32), 10, 0);
        } else if (c0 == 'l' && c1 == 'u') {
            printint(va_arg(ap, uint64), 10, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
            printint(va_arg(ap, uint64), 10, 0);
            i += 2;
        } else if (c0 == 'x') {
            printint(va_arg(ap, uint32), 16, 0);
        } else if (c0 == 'l' && c1 == 'x') {
            printint(va_arg(ap, uint64), 16, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
            printint(va_arg(ap, uint64), 16, 0);
            i += 2;
        } else if (c0 == 'p') {
            printptr(va_arg(ap, uint64));
        } else if (c0 == 'c') {
            consputc(va_arg(ap, uint));
        } else if (c0 == 's') {
            if ((s = va_arg(ap, char *)) == nullptr)
                s = "(null)";
            for (; *s; s++)
                consputc(*s);
        } else if (c0 == '%') {
            consputc('%');
        } else if (c0 == 0) {
            break;
        } else {
            // Print unknown % sequence to draw attention.
            consputc('%');
            consputc(c0);
        }
    }
    va_end(ap);

    if (panicking == 0)
        pr.lock.unlock();

    return 0;
}

void panic(const char *s) {
    panicking = 1;
    printf("panic: ");
    printf("%s\n", s);
    panicked = 1; // freeze uart output from other CPUs
    for (;;)
        ;
}

void printfinit() {
    pr.lock.init_lock("pr");
}

} // namespace xv6