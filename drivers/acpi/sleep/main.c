/*
 * sleep.c - ACPI sleep support.
 * 
 *  Copyright (c) 2000-2002 Patrick Mochel
 *
 *  Portions are
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/suspend.h>

#include <asm/acpi.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "sleep.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("sleep")

u8 sleep_states[ACPI_S_STATE_COUNT];

static void
acpi_power_off (void)
{
	acpi_enter_sleep_state_prep(ACPI_STATE_S5);
	ACPI_DISABLE_IRQS();
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

/**
 * acpi_system_restore_state - OS-specific restoration of state
 * @state:	sleep state we're exiting
 *
 * Note that if we're coming back from S4, the memory image should have already
 * been loaded from the disk and is already in place. (Otherwise how else would we
 * be here?).
 */
acpi_status
acpi_system_restore_state (
	u32			state)
{
	/* restore processor state
	 * We should only be here if we're coming back from STR or STD.
	 * And, in the case of the latter, the memory image should have already
	 * been loaded from disk.
	 */
	if (state > ACPI_STATE_S1)
		acpi_restore_state_mem();

	/* wait for power to come back */
	mdelay(10);

	/* turn all the devices back on */
	device_resume(RESUME_POWER_ON);

	/* enable interrupts once again */
	ACPI_ENABLE_IRQS();

	/* restore device context */
	device_resume(RESUME_RESTORE_STATE);

	if (dmi_broken & BROKEN_INIT_AFTER_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}

	return AE_OK;
}

/**
 * acpi_system_save_state - save OS specific state and power down devices
 * @state:	sleep state we're entering.
 *
 * This handles saving all context to memory, and possibly disk.
 * First, we call to the device driver layer to save device state.
 * Once we have that, we save whatevery processor and kernel state we
 * need to memory.
 * If we're entering S4, we then write the memory image to disk.
 *
 * Only then is it safe for us to power down devices, since we may need
 * the disks and upstream buses to write to.
 */
acpi_status
acpi_system_save_state(
	u32			state)
{
	int			error = 0;

	/* Send notification to devices that they will be suspended.
	 * If any device or driver cannot make the transition, either up
	 * or down, we'll get an error back.
	 */
	error = device_suspend(state, SUSPEND_NOTIFY);
	if (error)
		return AE_ERROR;

	if (state < ACPI_STATE_S5) {

		/* Tell devices to stop I/O and actually save their state.
		 * It is theoretically possible that something could fail,
		 * so handle that gracefully..
		 */
		error = device_suspend(state, SUSPEND_SAVE_STATE);
		if (error) {
			/* tell devices to restore state if they have
			 * it saved and to start taking I/O requests.
			 */
			device_resume(RESUME_RESTORE_STATE);
			return error;
		}

		/* flush caches */
		ACPI_FLUSH_CPU_CACHE();

		/* Do arch specific saving of state. */
		if (state > ACPI_STATE_S1) {
			error = acpi_save_state_mem();

			if (!error && (state == ACPI_STATE_S4))
				error = acpi_save_state_disk();

			if (error) {
				device_resume(RESUME_RESTORE_STATE);
				return error;
			}
		}
	}

	/* disable interrupts
	 * Note that acpi_suspend -- our caller -- will do this once we return.
	 * But, we want it done early, so we don't get any suprises during
	 * the device suspend sequence.
	 */
	ACPI_DISABLE_IRQS();

	/* Unconditionally turn off devices.
	 * Obvious if we enter a sleep state.
	 * If entering S5 (soft off), this should put devices in a
	 * quiescent state.
	 */
	error = device_suspend(state, SUSPEND_POWER_DOWN);

	/* We're pretty screwed if we got an error from this.
	 * We try to recover by simply calling our own restore_state
	 * function; see above for definition.
	 *
	 * If it's S5 though, go through with it anyway..
	 */
	if (error && state != ACPI_STATE_S5)
		acpi_system_restore_state(state);

	return error ? AE_ERROR : AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    acpi_system_suspend
 *
 * PARAMETERS:  %state: Sleep state to enter.
 *
 * RETURN:      acpi_status, whether or not we successfully entered and
 *              exited sleep.
 *
 * DESCRIPTION: Perform OS-specific action to enter sleep state.
 *              This is the final step in going to sleep, per spec.  If we
 *              know we're coming back (i.e. not entering S5), we save the
 *              processor flags. [ We'll have to save and restore them anyway,
 *              so we use the arch-agnostic save_flags and restore_flags
 *              here.]  We then set the place to return to in arch-specific
 *              globals using arch_set_return_point. Finally, we call the
 *              ACPI function to write the proper values to I/O ports.
 *
 ****************************************************************************/

acpi_status
acpi_system_suspend(
	u32			state)
{
	acpi_status		status = AE_ERROR;
	unsigned long		flags = 0;

	local_irq_save(flags);
	
	switch (state)
	{
	case ACPI_STATE_S1:
		barrier();
		status = acpi_enter_sleep_state(state);
		break;

	case ACPI_STATE_S2:
#ifdef CONFIG_SOFTWARE_SUSPEND
	case ACPI_STATE_S3:
		do_suspend_lowlevel(0);
#endif
		break;
	}
	local_irq_restore(flags);

	return status;
}


/**
 * acpi_suspend - OS-agnostic system suspend/resume support (S? states)
 * @state:	state we're entering
 *
 */
acpi_status
acpi_suspend (
	u32			state)
{
	acpi_status status;

	/* get out if state is invalid */
	if (state < ACPI_STATE_S1 || state > ACPI_STATE_S5)
		return AE_ERROR;

	freeze_processes();		/* device_suspend needs processes to be stopped */

	/* do we have a wakeup address for S2 and S3? */
	if (state == ACPI_STATE_S2 || state == ACPI_STATE_S3) {
		if (!acpi_wakeup_address)
			return AE_ERROR;
		acpi_set_firmware_waking_vector((acpi_physical_address) acpi_wakeup_address);
	}

	acpi_enter_sleep_state_prep(state);

	status = acpi_system_save_state(state);
	if (!ACPI_SUCCESS(status))
		return status;

	/* disable interrupts and flush caches */
	ACPI_DISABLE_IRQS();
	ACPI_FLUSH_CPU_CACHE();

	/* perform OS-specific sleep actions */
	status = acpi_system_suspend(state);

	/* Even if we failed to go to sleep, all of the devices are in an suspended
	 * mode. So, we run these unconditionaly to make sure we have a usable system
	 * no matter what.
	 */
	acpi_system_restore_state(state);
	acpi_leave_sleep_state(state);

	/* make sure interrupts are enabled */
	ACPI_ENABLE_IRQS();

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);
	thaw_processes();

	return status;
}

#if defined(CONFIG_MAGIC_SYSRQ) && defined(CONFIG_PM)

/* Simple wrapper calling power down function. */
static void acpi_sysrq_power_off(int key, struct pt_regs *pt_regs,
	struct tty_struct *tty)
{
	acpi_power_off();
}

struct sysrq_key_op sysrq_acpi_poweroff_op = {
	.handler =	&acpi_sysrq_power_off,
	.help_msg =	"Off",
	.action_msg =	"Power Off\n"
};

#endif  /* CONFIG_MAGIC_SYSRQ */

static int __init acpi_sleep_init(void)
{
	acpi_status		status = AE_OK;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_system_add_fs");

	if (acpi_disabled)
		return_VALUE(0);

	printk(KERN_INFO PREFIX "(supports");
	for (i=0; i<ACPI_S_STATE_COUNT; i++) {
		u8 type_a, type_b;
		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
			printk(" S%d", i);
		}
	}
	printk(")\n");

	acpi_sleep_proc_init();

	/* Install the soft-off (S5) handler. */
	if (sleep_states[ACPI_STATE_S5]) {
		pm_power_off = acpi_power_off;
		register_sysrq_key('o', &sysrq_acpi_poweroff_op);

		/* workaround: some systems don't claim S4 support, but they
                   do support S5 (power-down). That is all we need, so
		   indicate support. */
		sleep_states[ACPI_STATE_S4] = 1;
	}

	return_VALUE(0);
}

subsys_initcall(acpi_sleep_init);
