	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
_entry:
	# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096) //例如4核8线程, hartid为 0 1 2 3 4 5 6 7
        la sp, stack0 //取stack0地址为栈顶指针
        li a0, 1024*4 //4096

	csrr a1, mhartid 
        //读此时运行代码的 harts id
        //mhartid: ID of the hardware thread. Documented from "https://riscv.org/technical/specifications/" ISA Specification 下 Volume 2, Privileged Spec v. 20190608

        addi a1, a1, 1 //因为 harts id从0开始,故计算时需要+1
        mul a0, a0, a1 //此时运行代码的harts id 相对于 stack0 的地址
        add sp, sp, a0 //sp = stack0 + (hartid * 4096) 即当前hart id 的栈,sp指向栈顶
	# jump to start() in start.c
        call start //设置完毕,调用 start.c 中的 start // GOTO:start.c
spin:
        j spin
