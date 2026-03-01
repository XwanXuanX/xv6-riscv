#include "kernel/defs.h"
#include "kernel/mlfq.h"
#include "kernel/kalloc.h"
#include "kernel/slab.h"
#include "kernel/stl/ts_forward_list.h"
#include "kernel/stl/ts_list.h"
#include "kernel/proc_list.h"
#include "kernel/kstack_alloc.h"

namespace xv6 {

volatile static int started = 0;

static void self_test() {
    printf("Kernel self-test starting:\n");
    {
        test::slab_self_test(); // slab allocator self test
        printf("  * slab allocators selftest...\tOK\n");
    }
    {
        test::ts_forward_list_self_test(); // thread-safe list self test
        printf("  * ts_forward_list selftest...\tOK\n");
    }
    {
        test::ts_list_self_test(); // thread-safe doubly list self test
        printf("  * ts_list selftest...\t\tOK\n");
    }
    {
        test::kstack_self_test(); // kernel stack allocator self test
        printf("  * kstack_alloc selftest...\tOK\n");
    }
    printf("Kernel self-test passed\n\n");
}

// start() jumps here in supervisor mode on all CPUs.
void main() {
    if (cpuid() == 0) {
        consoleinit();
        printfinit();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");

        // initialize physical page allocator
        auto &page_alloc = page_allocator::instance();
        page_alloc.init();
        // create kernel page table
        pagetable_t kpt = kvminit();
        // turn on paging
        kvminithart();
        // process table
        proc_init();
        // trap vectors
        trapinit();
        // install kernel trap vector
        trapinithart();
        // set up interrupt controller
        plicinit();
        // ask PLIC for device interrupts
        plicinithart();
        // buffer cache
        binit();
        // inode table
        iinit();
        // file table
        fileinit();
        // emulated hard disk
        virtio_disk_init();
        // initialize process queue
        auto &feedback_q = multi_lvl_feedback_q::instance();
        feedback_q.init();
        // initialize all slab allocators
        slabs_init();
        // global process list
        auto &proc_list = process_list::instance();
        proc_list.init(kpt);
        // initialize with possible KVAs
        auto &kva_allocator = kstack_allocator::instance();
        kva_allocator.init();
        // subsystems self tests
        self_test();
        // first user process
        userinit();
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