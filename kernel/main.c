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
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    //satp设置内核页表，内核页表开始使用 ------->Kernel页表!!!!!
    kvminithart();   // turn on paging //w_satp(MAKE_SATP(kernel_pagetable));这里设置了s-mod时使用的页表，为kernel_pagetable内核页表。
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector  // S-Mode下的内核异常和中断地址
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}

//-1---------------------------------------------------------------------------------
// 物理地址RAM(最初态)
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area

// ...... unused RAM

//ME: 紧接着是 为64个进程分配的内核栈，每个占据两个页面 64*PAGESIZE*2
//ME: 内核的页表保存在最高地址处,占用大小一个页面 1*PAGESIZE       //PHYSTOP
//----------------------------------------------------------------------------------------

// 2
//内核态。64个进程共享一个内核页表
//然后紧邻64个进程的内核栈空间，每个进程 2*kstack,一个作栈，一个作保护页
// Kernel VM                                         |   PA
// 00001000 -- boot ROM, provided by qemu            |   same as in PHYSIC
// 02000000 -- CLINT                                 |   same as in PHYSIC
// 0C000000 -- PLIC                                  |   same as in PHYSIC
// 10000000 -- uart0                                 |   same as in PHYSIC
// 10001000 -- virtio disk                           |   same as in PHYSIC
// 80000000 -- boot ROM jumps here in machine mode   |   same as in PHYSIC
// kernel etext                                      |   same as in PHYSIC
//                                                   |
// ....   unused                                     |
//                                                   |
// 64个进程的内核栈,占据64*2*PAGESIZE,紧接TRAMPOLINE   |   紧接PHYSTOP内核页表处的已经分配了的64个进程的位置(见下面物理地址空间分布)
// TRAMPOLINE (MAXVM-PAGESIZE) 虚地址最大页面          |   (uint64)trampoline(in kernel etext somewhere)


//-3-----------------------------------------------------------------------------------------
//用户态。64个进程每个进程独立的页表和地址空间。

//第一个进程,设置其用户页表。【initcode 和 trapframe】
// Userinit VM                                       |   PA
// 000000000                                         |  mem(initcode)
//                                                   |
// ....   unused                                     |
//                                                   |
// TRAPFRAME                                         |   userinit第一个user进程分配的trapframe页，紧邻64进程内核栈页(见物理地址空间)
// TRAMPOLINE (MAXVM-PAGESIZE) 虚地址最大页面          |  (uint64)trampoline(in kernel etext somewhere)
//------------------------------------------------------------------------------------------
