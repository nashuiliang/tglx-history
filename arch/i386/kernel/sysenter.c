/*
 * linux/arch/i386/kernel/sysenter.c
 *
 * (C) Copyright 2002 Linus Torvalds
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/string.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>

extern asmlinkage void sysenter_entry(void);

/*
 * Create a per-cpu fake "SEP thread" stack, so that we can
 * enter the kernel without having to worry about things like
 * "current" etc not working (debug traps and NMI's can happen
 * before we can switch over to the "real" thread).
 *
 * Return the resulting fake stack pointer.
 */
struct fake_sep_struct {
	struct thread_info thread;
	struct task_struct task;
	unsigned char trampoline[32] __attribute__((aligned(1024)));
	unsigned char stack[0];
} __attribute__((aligned(8192)));
	
static struct fake_sep_struct *alloc_sep_thread(int cpu)
{
	struct fake_sep_struct *entry;

	entry = (struct fake_sep_struct *) __get_free_pages(GFP_ATOMIC, 1);
	if (!entry)
		return NULL;

	memset(entry, 0, PAGE_SIZE<<1);
	entry->thread.task = &entry->task;
	entry->task.thread_info = &entry->thread;
	entry->thread.preempt_count = 1;
	entry->thread.cpu = cpu;	

	return entry;
}

static void __init enable_sep_cpu(void *info)
{
	int cpu = get_cpu();
	struct fake_sep_struct *sep = alloc_sep_thread(cpu);
	unsigned long *esp0_ptr = &(init_tss + cpu)->esp0;
	unsigned long rel32;

	rel32 = (unsigned long) sysenter_entry - (unsigned long) (sep->trampoline+11);
	
	*(short *) (sep->trampoline+0) = 0x258b;		/* movl xxxxx,%esp */
	*(long **) (sep->trampoline+2) = esp0_ptr;
	*(char *)  (sep->trampoline+6) = 0xe9;			/* jmp rl32 */
	*(long *)  (sep->trampoline+7) = rel32;

	wrmsr(0x174, __KERNEL_CS, 0);				/* SYSENTER_CS_MSR */
	wrmsr(0x175, PAGE_SIZE*2 + (unsigned long) sep, 0);	/* SYSENTER_ESP_MSR */
	wrmsr(0x176, (unsigned long) &sep->trampoline, 0);	/* SYSENTER_EIP_MSR */

	printk("Enabling SEP on CPU %d\n", cpu);
	put_cpu();	
}

static int __init sysenter_setup(void)
{
	static const char int80[] = {
		0xcd, 0x80,		/* int $0x80 */
		0xc3			/* ret */
	};
	static const char sysent[] = {
		0x9c,			/* pushf */
		0x51,			/* push %ecx */
		0x52,			/* push %edx */
		0x55,			/* push %ebp */
		0x89, 0xe5,		/* movl %esp,%ebp */
		0x0f, 0x34,		/* sysenter */
	/* System call restart point is here! (SYSENTER_RETURN - 2) */
		0xeb, 0xfa,		/* jmp to "movl %esp,%ebp" */
	/* System call normal return point is here! (SYSENTER_RETURN in entry.S) */
		0x5d,			/* pop %ebp */
		0x5a,			/* pop %edx */
		0x59,			/* pop %ecx */
		0x9d,			/* popf - restore TF */
		0xc3			/* ret */
	};
	unsigned long page = get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY);
	memcpy((void *) page, int80, sizeof(int80));
	if (!boot_cpu_has(X86_FEATURE_SEP))
		return 0;

	memcpy((void *) page, sysent, sizeof(sysent));
	enable_sep_cpu(NULL);
	smp_call_function(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);