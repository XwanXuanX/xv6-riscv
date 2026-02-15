//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include "types.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "defs.h"
#include <array>

#define BACKSPACE 0x100  // erase the last output character
#define C(x) ((x) - '@') // Control-x

namespace xv6 {

//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printf and to echo input
// characters.
//
void consputc(const int c) {
    if (c == BACKSPACE) {
        // if the user typed backspace, overwrite with a space.
        uartputc_sync('\b');
        uartputc_sync(' ');
        uartputc_sync('\b');
    } else {
        uartputc_sync(c);
    }
}

struct {
    spinlock lock;

    // input circular buffer
#define INPUT_BUF_SIZE 128
    std::array<char, INPUT_BUF_SIZE> buf;
    uint r; // Read index
    uint w; // Write index
    uint e; // Edit index
} cons;

// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int consolewrite(const int user_src, const uint64 src, const int n) {
    std::array<char, 32> buf{}; // move batches from user space to uart.
    int i = 0;

    while (i < n) {
        int nn = sizeof(buf);
        if (nn > n - i) {
            nn = n - i;
        }
        if (either_copyin(buf.data(), user_src, src + i, nn) == -1) {
            break;
        }
        uartwrite(buf);
        i += nn;
    }

    return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int consoleread(const int user_dst, uint64 dst, int n) {
    char cbuf;

    const int target = n;
    cons.lock.lock();
    while (n > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        while (cons.r == cons.w) {
            if (killed(myproc())) {
                cons.lock.unlock();
                return -1;
            }
            sleep(&cons.r, &cons.lock);
        }

        const int c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

        if (c == C('D')) { // end-of-file
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                cons.r--;
            }
            break;
        }

        // copy the input byte to the user-space buffer.
        cbuf = c;
        if (either_copyout(user_dst, dst, &cbuf, 1) == -1) {
            break;
        }

        dst++;
        --n;

        if (c == '\n') {
            // a whole line has arrived, return to
            // the user-level read().
            break;
        }
    }
    cons.lock.unlock();

    return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void consoleintr(int c) {
    cons.lock.lock();

    switch (c) {
    case C('P'): // Print process list.
        procdump();
        break;
    case C('U'): // Kill line.
        while (cons.e != cons.w &&
               cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    case C('H'): // Backspace
    case '\x7f': // Delete key
        if (cons.e != cons.w) {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
            c = c == '\r' ? '\n' : c;

            // echo back to the user.
            consputc(c);

            // store for consumption by consoleread().
            cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

            if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
                // wake up consoleread() if a whole line (or end-of-file)
                // has arrived.
                cons.w = cons.e;
                wakeup(&cons.r);
            }
        }
        break;
    }

    cons.lock.unlock();
}

void consoleinit() {
    cons.lock.init_lock("cons");

    uartinit();

    // connect read and write system calls
    // to consoleread and consolewrite.
    dev[CONSOLE].read = consoleread;
    dev[CONSOLE].write = consolewrite;
}

} // namespace xv6
