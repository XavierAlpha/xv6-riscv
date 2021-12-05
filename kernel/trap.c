#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// 表明从User陷入后会执行到这里
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec); // 此时 line:126中设置的权限降低还未生效，仍旧在内核态。出现异常时陷入kerneltrap()

  struct proc *p = myproc(); // 依旧在第一个进程中
  
  // save user program counter.
  p->trapframe->epc = r_sepc(); // 此前 sepc为 0，epc此时指向0代表用户页表中VM=0处的la a0, init指令
  
  if(r_scause() == 8){ //导致陷入的原因，是 system call系统调用
    // system call

    if(p->killed) // 进程未被Kill
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4; // 暂存SYS_exec陷入结束后要返回的地址，此时为 exit: li a7, SYS_exit
    // .globl start
  // start:
     //   la a0, init
      //  la a1, argv
       // li a7, SYS_exec
       // ecall
  // exit:
       // li a7, SYS_exit

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();
    
    // syscall之前:该函数设置了以下信息:p->trapframe->epc=4;p->trapframe->a0=SYS_exec!!!

    syscall(); // 执行系统调用。p->trampframe->a0存放了系统调用函数返回值
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  //syscall之后:
  //p->pagetable为 new pagetable,argv的参数等push在此页表中
  //p->trapframe->a1 = sp 保存argv      (in exec.c line:103)
  //p->trapframe->a0保存argc            (in syscall.c line:141)
  //p->trapframe->epc = elf.entry       (in exec.c line:113) //elf文件中定义的entry,可自定义,一般是main
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc(); // 当前在scheduler()调度的进程

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off(); //关中断

  // send syscalls, interrupts, and exceptions to trampoline.S 指定该进程当发生异常等时使用的地址(与initcode.S中的ecall联系)
  w_stvec(TRAMPOLINE + (uservec - trampoline)); // 指向trampoline.S中uservec物理地址对应的虚拟地址，uservec与trampoine差一些对齐，这里直接指向uservec

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.   // 设置用户态如果再次进入内核时，内核使用的数据
  p->trapframe->kernel_satp = r_satp();         // kernel page table 【VMMAX处---PHYSTOP处】
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack【紧邻VMMAX的64内核栈---紧邻PHYSTOP64的内核栈】 //该进程对应的内核栈虚拟地址
  p->trapframe->kernel_trap = (uint64)usertrap; // 猜想：该进程从内核要陷入usertrap的地址
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);
  // w_sstatus 设置先前的mode为 User-mode, 当执行sret时，权限降低为 User-mode,同时sepc指定sret时要返回的地址，即该进程的initcode
  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);  // 设置CPU在 S mod 下的异常例如由user陷入kernel进入S模式时，当陷入结束sret执行时返回时的地址。即重新进入user
                              // 此时是0，即陷入返回执行用户虚地址处的initcode代码

                              //2 从syscall()回来后,epc=elf.entry,即sret时进入elf.entry
 
  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable); //satp保存着用户页表的PPN。satp控制着在s-mod下的地址翻译和保护。这里保持user页表
                              //2 从 syscall回来后,p->pagetable为新的 Usertable,保存着argv的参数
  
  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline); //fn为 trampoline.S 中 userret的地址

  // satp表示s-mod时使用user页表而非内核页表
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp); //调用 userret(TRAPFRAME, pagetable)
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

