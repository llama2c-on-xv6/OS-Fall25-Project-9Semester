#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
#if defined(LAB_LOCK)
    statsinit();
#endif
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
#ifdef LAB_NET
    pci_init();
    netinit();
#endif    
    userinit();      // first user process
        // *** FPU Initialization: Enable Floating-Point Unit Globally ***
    // We set the FS field (bits 13 and 14) of sstatus to Initial (01).
    // This allows the use of FPU in S-mode and U-mode.
    uint64 sstatus = r_sstatus();
    sstatus &= ~(3L << 13); // Clear the FS field (bits 13, 14)
    sstatus |= (1L << 13);  // Set FS to Initial (01)
    w_sstatus(sstatus);
    // **********************************

    __sync_synchronize();
    started = 1;
#ifdef KCSAN
    kcsaninit();
#endif
    __sync_synchronize();
    started = 1;
  } else {
    while(atomic_read4((int *) &started) == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

#ifdef LAB_LOCK
  rwspinlock_test();
#endif
  scheduler();        
}
