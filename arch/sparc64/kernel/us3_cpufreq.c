/* us3_cpufreq.c: UltraSPARC-III cpu frequency support
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 *
 * Many thanks to Dominik Brodowski for fixing up the cpufreq
 * infrastructure in order to make this driver easier to implement.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <asm/head.h>

static struct cpufreq_driver *cpufreq_us3_driver;

struct us3_freq_percpu_info {
	struct cpufreq_frequency_table table[4];
	unsigned long udelay_val_ref;
	unsigned long clock_tick_ref;
	unsigned int ref_freq;
};

/* Indexed by cpu number. */
static struct us3_freq_percpu_info *us3_freq_table;

/* UltraSPARC-III has three dividers: 1, 2, and 32.  These are controlled
 * in the Safari config register.
 */
#define SAFARI_CFG_DIV_1	0x0000000000000000UL
#define SAFARI_CFG_DIV_2	0x0000000040000000UL
#define SAFARI_CFG_DIV_32	0x0000000080000000UL
#define SAFARI_CFG_DIV_MASK	0x00000000C0000000UL

static unsigned long read_safari_cfg(void)
{
	unsigned long ret;

	__asm__ __volatile__("ldxa	[%%g0] %1, %0"
			     : "=&r" (ret)
			     : "i" (ASI_SAFARI_CONFIG));
	return ret;
}

static void write_safari_cfg(unsigned long val)
{
	__asm__ __volatile__("stxa	%0, [%%g0] %1\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (val), "i" (ASI_SAFARI_CONFIG)
			     : "memory");
}

#ifndef CONFIG_SMP
extern unsigned long up_clock_tick;
unsigned long clock_tick_ref;
unsigned int ref_freq;
#endif

static __inline__ unsigned long get_clock_tick(unsigned int cpu)
{
#ifdef CONFIG_SMP
	if (us3_freq_table[cpu].clock_tick_ref)
		return us3_freq_table[cpu].clock_tick_ref;
	return cpu_data[cpu].clock_tick;
#else
	if (clock_tick_ref)
		return clock_tick_ref;
	return up_clock_tick;
#endif
}

static int us3_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned int cpu = freq->cpu;

#ifdef CONFIG_SMP
	if (!us3_freq_table[cpu].ref_freq) {
		us3_freq_table[cpu].ref_freq = freq->old;
		us3_freq_table[cpu].udelay_val_ref = cpu_data[cpu].udelay_val;
		us3_freq_table[cpu].clock_tick_ref = cpu_data[cpu].clock_tick;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new)) {
		cpu_data[cpu].udelay_val =
			cpufreq_scale(us3_freq_table[cpu].udelay_val_ref,
				      us3_freq_table[cpu].ref_freq,
				      freq->new);
		cpu_data[cpu].clock_tick =
			cpufreq_scale(us3_freq_table[cpu].clock_tick_ref,
				      us3_freq_table[cpu].ref_freq,
				      freq->new);
	}
#else
	/* In the non-SMP case, kernel/cpufreq.c takes care of adjusting
	 * loops_per_jiffy.
	 */
	if (!ref_freq) {
		ref_freq = freq->old;
		clock_tick_ref = up_clock_tick;
	}
	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new))
		up_clock_tick = cpufreq_scale(clock_tick_ref, ref_freq, freq->new);
#endif

	return 0;
}

static struct notifier_block us3_cpufreq_notifier_block = {
	.notifier_call	= us3_cpufreq_notifier
};

static unsigned long get_current_freq(unsigned int cpu, unsigned long safari_cfg)
{
	unsigned long clock_tick = get_clock_tick(cpu);
	unsigned long ret;

	switch (safari_cfg & SAFARI_CFG_DIV_MASK) {
	case SAFARI_CFG_DIV_1:
		ret = clock_tick / 1;
		break;
	case SAFARI_CFG_DIV_2:
		ret = clock_tick / 2;
		break;
	case SAFARI_CFG_DIV_32:
		ret = clock_tick / 32;
		break;
	default:
		BUG();
	};

	return ret;
}

static void us3_set_cpu_divider_index(unsigned int cpu, unsigned int index)
{
	unsigned long new_bits, new_freq, reg, cpus_allowed;
	struct cpufreq_freqs freqs;

	if (!cpu_online(cpu))
		return;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, (1UL << cpu));

	new_freq = get_clock_tick(cpu);
	switch (index) {
	case 0:
		new_bits = SAFARI_CFG_DIV_1;
		new_freq /= 1;
		break;
	case 1:
		new_bits = SAFARI_CFG_DIV_2;
		new_freq /= 2;
		break;
	case 2:
		new_bits = SAFARI_CFG_DIV_32;
		new_freq /= 32;
		break;

	default:
		BUG();
	};

	reg = read_safari_cfg();

	freqs.old = get_current_freq(cpu, reg);
	freqs.new = new_freq;
	freqs.cpu = cpu;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	reg &= ~SAFARI_CFG_DIV_MASK;
	reg |= new_bits;
	write_safari_cfg(reg);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	set_cpus_allowed(current, cpus_allowed);
}

static int us3freq_setpolicy(struct cpufreq_policy *policy)
{
	unsigned int new_index = 0;

	if (cpufreq_frequency_table_setpolicy(policy,
					      &us3_freq_table[policy->cpu].table[0],
					      &new_index))
		return -EINVAL;

	us3_set_cpu_divider_index(policy->cpu, new_index);

	return 0;
}

static int us3freq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      &us3_freq_table[policy->cpu].table[0]);
}

static int __init us3freq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	unsigned long clock_tick = get_clock_tick(cpu);
	struct cpufreq_frequency_table *table =
		&us3_freq_table[cpu].table[0];

	table[0].index = 0;
	table[0].frequency = clock_tick / 1;
	table[1].index = 1;
	table[1].frequency = clock_tick / 2;
	table[2].index = 2;
	table[2].frequency = clock_tick / 32;
	table[3].index = 0;
	table[3].frequency = CPUFREQ_TABLE_END;

	policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	policy->cpuinfo.transition_latency = 0;

	return cpufreq_frequency_table_cpuinfo(policy, table);
}

static int __exit us3freq_cpu_exit(struct cpufreq_policy *policy)
{
	if (cpufreq_us3_driver)
		us3_set_cpu_divider_index(policy->cpu, 0);

	return 0;
}

static int __init us3freq_init(void)
{
	unsigned long manuf, impl, ver;
	int ret;

	__asm__("rdpr %%ver, %0" : "=r" (ver));
	manuf = ((ver >> 48) & 0xffff);
	impl  = ((ver >> 32) & 0xffff);

	if (manuf == CHEETAH_MANUF &&
	    (impl == CHEETAH_IMPL || impl == CHEETAH_PLUS_IMPL)) {
		struct cpufreq_driver *driver;

		cpufreq_register_notifier(&us3_cpufreq_notifier_block,
					  CPUFREQ_TRANSITION_NOTIFIER);

		ret = -ENOMEM;
		driver = kmalloc(sizeof(struct cpufreq_driver), GFP_KERNEL);
		if (!driver)
			goto err_out;
		memset(driver, 0, sizeof(*driver));

		us3_freq_table = kmalloc(
			(NR_CPUS * sizeof(struct us3_freq_percpu_info)),
			GFP_KERNEL);
		if (!us3_freq_table)
			goto err_out;

		memset(us3_freq_table, 0,
		       (NR_CPUS * sizeof(struct us3_freq_percpu_info)));

		driver->verify = us3freq_verify;
		driver->setpolicy = us3freq_setpolicy;
		driver->init = us3freq_cpu_init;
		driver->exit = us3freq_cpu_exit;
		strcpy(driver->name, "UltraSPARC-III");

		cpufreq_us3_driver = driver;
		ret = cpufreq_register_driver(driver);
		if (ret)
			goto err_out;

		return 0;

err_out:
		if (driver) {
			kfree(driver);
			cpufreq_us3_driver = NULL;
		}
		if (us3_freq_table) {
			kfree(us3_freq_table);
			us3_freq_table = NULL;
		}
		cpufreq_unregister_notifier(&us3_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);
		return ret;
	}

	return -ENODEV;
}

static void __exit us3freq_exit(void)
{
	if (cpufreq_us3_driver) {
		cpufreq_unregister_driver(cpufreq_us3_driver);
		cpufreq_unregister_notifier(&us3_cpufreq_notifier_block,
					    CPUFREQ_TRANSITION_NOTIFIER);

		kfree(cpufreq_us3_driver);
		cpufreq_us3_driver = NULL;
		kfree(us3_freq_table);
		us3_freq_table = NULL;
	}
}

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("cpufreq driver for UltraSPARC-III");
MODULE_LICENSE("GPL");

module_init(us3freq_init);
module_exit(us3freq_exit);