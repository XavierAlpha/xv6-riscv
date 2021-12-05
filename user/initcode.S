# Initial process that execs /init.
# This code runs in user space.

#include "syscall.h"

# exec(init, argv)
.globl start
start:
        la a0, init  # 从trampoline.S的userret而来。运行在 User-mode。运行在User-mode的代码不需要操作页表，这是内核的事情，故只有内核态时才需要知道satp的值
        la a1, argv
        li a7, SYS_exec
        ecall // ecall执行触发系统调用，陷入。此前 trap.c line:114指定了当发生异常时要进入的地址。为 trampoline.S中的uservec

# for(;;) exit();
exit:
        li a7, SYS_exit
        ecall
        jal exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .long init
  .long 0
