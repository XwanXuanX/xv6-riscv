#pragma once

namespace xv6 {

struct context;

// External symbols from linker script and assembly.
extern "C" {
extern char end[];   // first address after kernel
extern char etext[]; // first address after kernel code
extern char trampoline[];
extern char userret[];
extern char uservec[];
void kernelvec();
void swtch(context *, context *);
void start();
void main();
void kerneltrap();
}

} // namespace xv6
