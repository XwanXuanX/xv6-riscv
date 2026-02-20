#include "defs.h"
#include "mlfq.h"
#include "kalloc.h"
#include "slab.h"

namespace xv6 {

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void main() {
    if (cpuid() == 0) {
        consoleinit();
        printfinit();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");

        auto &page_alloc = page_allocator::instance();
        page_alloc.init();  // physical page allocator
        kvminit();          // create kernel page table
        kvminithart();      // turn on paging
        procinit();         // process table
        trapinit();         // trap vectors
        trapinithart();     // install kernel trap vector
        plicinit();         // set up interrupt controller
        plicinithart();     // ask PLIC for device interrupts
        binit();            // buffer cache
        iinit();            // inode table
        fileinit();         // file table
        virtio_disk_init(); // emulated hard disk
        auto &feedback_q = multi_lvl_feedback_q::instance();
        feedback_q.init();      // initialize MLFQ
        slabs_init();           // initialize all slab allocators
        test::slab_self_test(); // slab allocator self test
        userinit();             // first user process
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("hart %d starting\n", cpuid());
        kvminithart();  // turn on paging
        trapinithart(); // install kernel trap vector
        plicinithart(); // ask PLIC for device interrupts
    }

    scheduler();
}

} // namespace xv6