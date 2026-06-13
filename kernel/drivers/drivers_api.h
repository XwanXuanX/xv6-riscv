#pragma once

#include <span>

namespace xv6 {

struct buf;

// console.cc
void consoleinit();
void consoleintr(int);
void consputc(int);

// uart.cc
void uartinit();
void uartintr();
void uartwrite(std::span<char>);
void uartputc_sync(int);
int uartgetc();

// plic.cc
void plicinit();
void plicinithart();
int plic_claim();
void plic_complete(int);

// virtio_disk.cc
void virtio_disk_init();
void virtio_disk_rw(buf *, int);
void virtio_disk_intr();

} // namespace xv6
