/*
 * sysctl.c: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 * Added /proc support, Dec 1995
 * Added bdflush entry and intvec min/max checking, 2/23/96, Tom Dyas.
 * Added hooks for /proc/sys/net (minor, minor patch), 96/4/1, Mike Shaver.
 * Added kernel/java-{interpreter,appletviewer}, 96/5/10, Mike Shaver.
 * Dynamic registration fixes, Stephen Tweedie.
 * Added kswapd-interval, ctrl-alt-del, printk stuff, 1/8/97, Chris Horn.
 * Made sysctl support optional via CONFIG_SYSCTL, 1/10/97, Chris
 *  Horn.
 * Added proc_doulongvec_ms_jiffies_minmax, 09/08/99, Carlos H. Bauer.
 * Added proc_doulongvec_minmax, 09/08/99, Carlos H. Bauer.
 * Changed linked lists to use list.h instead of lists.h, 02/24/00, Bill
 *  Wendling.
 * The list_for_each() macro wasn't appropriate for the sysctl loop.
 *  Removed it and replaced it with older style, 03/23/00, Bill Wendling
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/capability.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/highuid.h>
#include <linux/writeback.h>
#include <linux/hugetlb.h>
#include <linux/security.h>
#include <linux/initrd.h>
#include <asm/uaccess.h>

#ifdef CONFIG_ROOT_NFS
#include <linux/nfs_fs.h>
#endif

#if defined(CONFIG_SYSCTL)

/* External variables not in a header file. */
extern int panic_timeout;
extern int C_A_D;
extern int sysctl_overcommit_memory;
extern int sysctl_overcommit_ratio;
extern int max_threads;
extern atomic_t nr_queued_signals;
extern int max_queued_signals;
extern int sysrq_enabled;
extern int core_uses_pid;
extern char core_pattern[];
extern int cad_pid;
extern int pid_max;
extern int sysctl_lower_zone_protection;
extern int min_free_kbytes;

/* this is needed for the proc_dointvec_minmax for [fs_]overflow UID and GID */
static int maxolduid = 65535;
static int minolduid;

#ifdef CONFIG_KMOD
extern char modprobe_path[];
#endif
#ifdef CONFIG_HOTPLUG
extern char hotplug_path[];
#endif
#ifdef CONFIG_CHR_DEV_SG
extern int sg_big_buff;
#endif
#ifdef CONFIG_SYSVIPC
extern size_t shm_ctlmax;
extern size_t shm_ctlall;
extern int shm_ctlmni;
extern int msg_ctlmax;
extern int msg_ctlmnb;
extern int msg_ctlmni;
extern int sem_ctls[];
#endif

#ifdef __sparc__
extern char reboot_command [];
extern int stop_a_enabled;
#endif

#ifdef __hppa__
extern int pwrsw_enabled;
extern int unaligned_enabled;
#endif

#ifdef CONFIG_ARCH_S390
#ifdef CONFIG_MATHEMU
extern int sysctl_ieee_emulation_warnings;
#endif
extern int sysctl_userprocess_debug;
#endif

#if defined(CONFIG_PPC32) && defined(CONFIG_6xx)
extern unsigned long powersave_nap;
int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp);
#endif

#ifdef CONFIG_BSD_PROCESS_ACCT
extern int acct_parm[];
#endif

static int parse_table(int __user *, int, void __user *, size_t __user *, void __user *, size_t,
		       ctl_table *, void **);
static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp);

static ctl_table root_table[];
static struct ctl_table_header root_table_header =
	{ root_table, LIST_HEAD_INIT(root_table_header.ctl_entry) };

static ctl_table kern_table[];
static ctl_table vm_table[];
#ifdef CONFIG_NET
extern ctl_table net_table[];
#endif
static ctl_table proc_table[];
static ctl_table fs_table[];
static ctl_table debug_table[];
static ctl_table dev_table[];
extern ctl_table random_table[];

/* /proc declarations: */

#ifdef CONFIG_PROC_FS

static ssize_t proc_readsys(struct file *, char __user *, size_t, loff_t *);
static ssize_t proc_writesys(struct file *, const char __user *, size_t, loff_t *);
static int proc_sys_permission(struct inode *, int, struct nameidata *);

struct file_operations proc_sys_file_operations = {
	.read		= proc_readsys,
	.write		= proc_writesys,
};

static struct inode_operations proc_sys_inode_operations = {
	.permission	= proc_sys_permission,
};

extern struct proc_dir_entry *proc_sys_root;

static void register_proc_table(ctl_table *, struct proc_dir_entry *);
static void unregister_proc_table(ctl_table *, struct proc_dir_entry *);
#endif

/* The default sysctl tables: */

static ctl_table root_table[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= kern_table,
	},
	{
		.ctl_name	= CTL_VM,
		.procname	= "vm",
		.mode		= 0555,
		.child		= vm_table,
	},
#ifdef CONFIG_NET
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= net_table,
	},
#endif
	{
		.ctl_name	= CTL_PROC,
		.procname	= "proc",
		.mode		= 0555,
		.child		= proc_table,
	},
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{
		.ctl_name	= CTL_DEBUG,
		.procname	= "debug",
		.mode		= 0555,
		.child		= debug_table,
	},
	{
		.ctl_name	= CTL_DEV,
		.procname	= "dev",
		.mode		= 0555,
		.child		= dev_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table kern_table[] = {
	{
		.ctl_name	= KERN_OSTYPE,
		.procname	= "ostype",
		.data		= system_utsname.sysname,
		.maxlen		= 64,
		.mode		= 0444,
		.proc_handler	= &proc_doutsstring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_OSRELEASE,
		.procname	= "osrelease",
		.data		= system_utsname.release,
		.maxlen		= 64,
		.mode		= 0444,
		.proc_handler	= &proc_doutsstring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_VERSION,
		.procname	= "version",
		.data		= system_utsname.version,
		.maxlen		= 64,
		.mode		= 0444,
		.proc_handler	= &proc_doutsstring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_NODENAME,
		.procname	= "hostname",
		.data		= system_utsname.nodename,
		.maxlen		= 64,
		.mode		= 0644,
		.proc_handler	= &proc_doutsstring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_DOMAINNAME,
		.procname	= "domainname",
		.data		= system_utsname.domainname,
		.maxlen		= 64,
		.mode		= 0644,
		.proc_handler	= &proc_doutsstring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_PANIC,
		.procname	= "panic",
		.data		= &panic_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_CORE_USES_PID,
		.procname	= "core_uses_pid",
		.data		= &core_uses_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_CORE_PATTERN,
		.procname	= "core_pattern",
		.data		= core_pattern,
		.maxlen		= 64,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_TAINTED,
		.procname	= "tainted",
		.data		= &tainted,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_CAP_BSET,
		.procname	= "cap-bound",
		.data		= &cap_bset,
		.maxlen		= sizeof(kernel_cap_t),
		.mode		= 0600,
		.proc_handler	= &proc_dointvec_bset,
	},
#ifdef CONFIG_BLK_DEV_INITRD
	{
		.ctl_name	= KERN_REALROOTDEV,
		.procname	= "real-root-dev",
		.data		= &real_root_dev,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef __sparc__
	{
		.ctl_name	= KERN_SPARC_REBOOT,
		.procname	= "reboot-cmd",
		.data		= reboot_command,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_SPARC_STOP_A,
		.procname	= "stop-a",
		.data		= &stop_a_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef __hppa__
	{
		.ctl_name	= KERN_HPPA_PWRSW,
		.procname	= "soft-power",
		.data		= &pwrsw_enabled,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_HPPA_UNALIGNED,
		.procname	= "unaligned-trap",
		.data		= &unaligned_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef __hppa__
	{KERN_HPPA_PWRSW, "soft-power", &pwrsw_enabled, sizeof (int),
	 0644, NULL, &proc_dointvec},
	{KERN_HPPA_UNALIGNED, "unaligned-trap", &unaligned_enabled, sizeof (int),
	 0644, NULL, &proc_dointvec},
#endif
#if defined(CONFIG_PPC32) && defined(CONFIG_6xx)
	{
		.ctl_name	= KERN_PPC_POWERSAVE_NAP,
		.procname	= "powersave-nap",
		.data		= &powersave_nap,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_PPC_L2CR,
		.procname	= "l2cr",
		.mode		= 0644,
		.proc_handler	= &proc_dol2crvec,
	},
#endif
	{
		.ctl_name	= KERN_CTLALTDEL,
		.procname	= "ctrl-alt-del",
		.data		= &C_A_D,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_PRINTK,
		.procname	= "printk",
		.data		= &console_loglevel,
		.maxlen		= 4*sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_KMOD
	{
		.ctl_name	= KERN_MODPROBE,
		.procname	= "modprobe",
		.data		= &modprobe_path,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
#endif
#ifdef CONFIG_HOTPLUG
	{
		.ctl_name	= KERN_HOTPLUG,
		.procname	= "hotplug",
		.data		= &hotplug_path,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
#endif
#ifdef CONFIG_CHR_DEV_SG
	{
		.ctl_name	= KERN_SG_BIG_BUFF,
		.procname	= "sg-big-buff",
		.data		= &sg_big_buff,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	{
		.ctl_name	= KERN_ACCT,
		.procname	= "acct",
		.data		= &acct_parm,
		.maxlen		= 3*sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_RTSIGNR,
		.procname	= "rtsig-nr",
		.data		= &nr_queued_signals,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_RTSIGMAX,
		.procname	= "rtsig-max",
		.data		= &max_queued_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_SYSVIPC
	{
		.ctl_name	= KERN_SHMMAX,
		.procname	= "shmmax",
		.data		= &shm_ctlmax,
		.maxlen		= sizeof (size_t),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= KERN_SHMALL,
		.procname	= "shmall",
		.data		= &shm_ctlall,
		.maxlen		= sizeof (size_t),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= KERN_SHMMNI,
		.procname	= "shmmni",
		.data		= &shm_ctlmni,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_MSGMAX,
		.procname	= "msgmax",
		.data		= &msg_ctlmax,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_MSGMNI,
		.procname	= "msgmni",
		.data		= &msg_ctlmni,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_MSGMNB,
		.procname	=  "msgmnb",
		.data		= &msg_ctlmnb,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_SEM,
		.procname	= "sem",
		.data		= &sem_ctls,
		.maxlen		= 4*sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_MAGIC_SYSRQ
	{
		.ctl_name	= KERN_SYSRQ,
		.procname	= "sysrq",
		.data		= &sysrq_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_CADPID,
		.procname	= "cad_pid",
		.data		= &cad_pid,
		.maxlen		= sizeof (int),
		.mode		= 0600,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_MAX_THREADS,
		.procname	= "threads-max",
		.data		= &max_threads,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_RANDOM,
		.procname	= "random",
		.mode		= 0555,
		.child		= random_table,
	},
	{
		.ctl_name	= KERN_OVERFLOWUID,
		.procname	= "overflowuid",
		.data		= &overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= KERN_OVERFLOWGID,
		.procname	= "overflowgid",
		.data		= &overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_ARCH_S390
#ifdef CONFIG_MATHEMU
	{
		.ctl_name	= KERN_IEEE_EMULATION_WARNINGS,
		.procname	= "ieee_emulation_warnings",
		.data		= &sysctl_ieee_emulation_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_S390_USER_DEBUG_LOGGING,
		.procname	= "userprocess_debug",
		.data		= &sysctl_userprocess_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_PIDMAX,
		.procname	= "pid_max",
		.data		= &pid_max,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_PANIC_ON_OOPS,
		.procname	= "panic_on_oops",
		.data		= &panic_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

/* Constants for minimum and maximum testing in vm_table.
   We use these as one-element integer vectors. */
static int zero;
static int one_hundred = 100;


static ctl_table vm_table[] = {
	{
		.ctl_name	= VM_OVERCOMMIT_MEMORY,
		.procname	= "overcommit_memory",
		.data		= &sysctl_overcommit_memory,
		.maxlen		= sizeof(sysctl_overcommit_memory),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_OVERCOMMIT_RATIO,
		.procname	= "overcommit_ratio",
		.data		= &sysctl_overcommit_ratio,
		.maxlen		= sizeof(sysctl_overcommit_ratio),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_PAGE_CLUSTER,
		.procname	= "page-cluster", 
		.data		= &page_cluster,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_DIRTY_BACKGROUND,
		.procname	= "dirty_background_ratio",
		.data		= &dirty_background_ratio,
		.maxlen		= sizeof(dirty_background_ratio),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.ctl_name	= VM_DIRTY_RATIO,
		.procname	= "dirty_ratio",
		.data		= &vm_dirty_ratio,
		.maxlen		= sizeof(vm_dirty_ratio),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.ctl_name	= VM_DIRTY_WB_CS,
		.procname	= "dirty_writeback_centisecs",
		.data		= &dirty_writeback_centisecs,
		.maxlen		= sizeof(dirty_writeback_centisecs),
		.mode		= 0644,
		.proc_handler	= &dirty_writeback_centisecs_handler,
	},
	{
		.ctl_name	= VM_DIRTY_EXPIRE_CS,
		.procname	= "dirty_expire_centisecs",
		.data		= &dirty_expire_centisecs,
		.maxlen		= sizeof(dirty_expire_centisecs),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_NR_PDFLUSH_THREADS,
		.procname	= "nr_pdflush_threads",
		.data		= &nr_pdflush_threads,
		.maxlen		= sizeof nr_pdflush_threads,
		.mode		= 0444 /* read-only*/,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_SWAPPINESS,
		.procname	= "swappiness",
		.data		= &vm_swappiness,
		.maxlen		= sizeof(vm_swappiness),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#ifdef CONFIG_HUGETLB_PAGE
	 {
		.ctl_name	= VM_HUGETLB_PAGES,
		.procname	= "nr_hugepages",
		.data		= &htlbpage_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &hugetlb_sysctl_handler,
	 },
#endif
	{
		.ctl_name	= VM_LOWER_ZONE_PROTECTION,
		.procname	= "lower_zone_protection",
		.data		= &sysctl_lower_zone_protection,
		.maxlen		= sizeof(sysctl_lower_zone_protection),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{
		.ctl_name	= VM_MIN_FREE_KBYTES,
		.procname	= "min_free_kbytes",
		.data		= &min_free_kbytes,
		.maxlen		= sizeof(min_free_kbytes),
		.mode		= 0644,
		.proc_handler	= &min_free_kbytes_sysctl_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{ .ctl_name = 0 }
};

static ctl_table proc_table[] = {
	{ .ctl_name = 0 }
};

static ctl_table fs_table[] = {
	{
		.ctl_name	= FS_NRINODE,
		.procname	= "inode-nr",
		.data		= &inodes_stat,
		.maxlen		= 2*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_STATINODE,
		.procname	= "inode-state",
		.data		= &inodes_stat,
		.maxlen		= 7*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_NRFILE,
		.procname	= "file-nr",
		.data		= &files_stat,
		.maxlen		= 3*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_MAXFILE,
		.procname	= "file-max",
		.data		= &files_stat.max_files,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DENTRY,
		.procname	= "dentry-state",
		.data		= &dentry_stat,
		.maxlen		= 6*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_OVERFLOWUID,
		.procname	= "overflowuid",
		.data		= &fs_overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= FS_OVERFLOWGID,
		.procname	= "overflowgid",
		.data		= &fs_overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= FS_LEASES,
		.procname	= "leases-enable",
		.data		= &leases_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DIR_NOTIFY,
		.procname	= "dir-notify-enable",
		.data		= &dir_notify_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_LEASE_TIME,
		.procname	= "lease-break-time",
		.data		= &lease_break_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static ctl_table debug_table[] = {
	{ .ctl_name = 0 }
};

static ctl_table dev_table[] = {
	{ .ctl_name = 0 }
};  

extern void init_irq_proc (void);

void __init sysctl_init(void)
{
#ifdef CONFIG_PROC_FS
	register_proc_table(root_table, proc_sys_root);
	init_irq_proc();
#endif
}

int do_sysctl(int __user *name, int nlen, void __user *oldval, size_t __user *oldlenp,
	       void __user *newval, size_t newlen)
{
	struct list_head *tmp;

	if (nlen <= 0 || nlen >= CTL_MAXNAME)
		return -ENOTDIR;
	if (oldval) {
		int old_len;
		if (!oldlenp || get_user(old_len, oldlenp))
			return -EFAULT;
	}
	tmp = &root_table_header.ctl_entry;
	do {
		struct ctl_table_header *head =
			list_entry(tmp, struct ctl_table_header, ctl_entry);
		void *context = NULL;
		int error = parse_table(name, nlen, oldval, oldlenp, 
					newval, newlen, head->ctl_table,
					&context);
		if (context)
			kfree(context);
		if (error != -ENOTDIR)
			return error;
		tmp = tmp->next;
	} while (tmp != &root_table_header.ctl_entry);
	return -ENOTDIR;
}

asmlinkage long sys_sysctl(struct __sysctl_args __user *args)
{
	struct __sysctl_args tmp;
	int name[2];
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;
	
	if (tmp.nlen != 2 || copy_from_user(name, tmp.name, sizeof(name)) ||
	    name[0] != CTL_KERN || name[1] != KERN_VERSION) { 
		int i;
		printk(KERN_INFO "%s: numerical sysctl ", current->comm); 
		for (i = 0; i < tmp.nlen; i++) {
			int n;
			
			if (get_user(n, tmp.name+i)) {
				printk("? ");
			} else {
				printk("%d ", n);
			}
		}
		printk("is obsolete.\n");
	} 

	lock_kernel();
	error = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp,
			  tmp.newval, tmp.newlen);
	unlock_kernel();
	return error;
}

/*
 * ctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (!current->euid)
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((mode & op & 0007) == op)
		return 0;
	return -EACCES;
}

static inline int ctl_perm(ctl_table *table, int op)
{
	int error;
	error = security_sysctl(table, op);
	if (error)
		return error;
	return test_perm(table->mode, op);
}

static int parse_table(int __user *name, int nlen,
		       void __user *oldval, size_t __user *oldlenp,
		       void __user *newval, size_t newlen,
		       ctl_table *table, void **context)
{
	int n;
repeat:
	if (!nlen)
		return -ENOTDIR;
	if (get_user(n, name))
		return -EFAULT;
	for ( ; table->ctl_name; table++) {
		if (n == table->ctl_name || table->ctl_name == CTL_ANY) {
			int error;
			if (table->child) {
				if (ctl_perm(table, 001))
					return -EPERM;
				if (table->strategy) {
					error = table->strategy(
						table, name, nlen,
						oldval, oldlenp,
						newval, newlen, context);
					if (error)
						return error;
				}
				name++;
				nlen--;
				table = table->child;
				goto repeat;
			}
			error = do_sysctl_strategy(table, name, nlen,
						   oldval, oldlenp,
						   newval, newlen, context);
			return error;
		}
	}
	return -ENOTDIR;
}

/* Perform the actual read/write of a sysctl table entry. */
int do_sysctl_strategy (ctl_table *table, 
			int __user *name, int nlen,
			void __user *oldval, size_t __user *oldlenp,
			void __user *newval, size_t newlen, void **context)
{
	int op = 0, rc;
	size_t len;

	if (oldval)
		op |= 004;
	if (newval) 
		op |= 002;
	if (ctl_perm(table, op))
		return -EPERM;

	if (table->strategy) {
		rc = table->strategy(table, name, nlen, oldval, oldlenp,
				     newval, newlen, context);
		if (rc < 0)
			return rc;
		if (rc > 0)
			return 0;
	}

	/* If there is no strategy routine, or if the strategy returns
	 * zero, proceed with automatic r/w */
	if (table->data && table->maxlen) {
		if (oldval && oldlenp) {
			get_user(len, oldlenp);
			if (len) {
				if (len > table->maxlen)
					len = table->maxlen;
				if(copy_to_user(oldval, table->data, len))
					return -EFAULT;
				if(put_user(len, oldlenp))
					return -EFAULT;
			}
		}
		if (newval && newlen) {
			len = newlen;
			if (len > table->maxlen)
				len = table->maxlen;
			if(copy_from_user(table->data, newval, len))
				return -EFAULT;
		}
	}
	return 0;
}

/**
 * register_sysctl_table - register a sysctl hierarchy
 * @table: the top-level table structure
 * @insert_at_head: whether the entry should be inserted in front or at the end
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. An entry with a ctl_name of 0 terminates the table. 
 *
 * The members of the &ctl_table structure are used as follows:
 *
 * ctl_name - This is the numeric sysctl value used by sysctl(2). The number
 *            must be unique within that level of sysctl
 *
 * procname - the name of the sysctl file under /proc/sys. Set to %NULL to not
 *            enter a sysctl file
 *
 * data - a pointer to data for use by proc_handler
 *
 * maxlen - the maximum size in bytes of the data
 *
 * mode - the file permissions for the /proc/sys file, and for sysctl(2)
 *
 * child - a pointer to the child sysctl table if this entry is a directory, or
 *         %NULL.
 *
 * proc_handler - the text handler routine (described below)
 *
 * strategy - the strategy routine (described below)
 *
 * de - for internal use by the sysctl routines
 *
 * extra1, extra2 - extra pointers usable by the proc handler routines
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.
 *
 * sysctl(2) can automatically manage read and write requests through
 * the sysctl table.  The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 *
 * More sophisticated management can be enabled by the provision of a
 * strategy routine with the table entry.  This will be called before
 * any automatic read or write of the data is performed.
 *
 * The strategy routine may return
 *
 * < 0 - Error occurred (error is passed to user process)
 *
 * 0   - OK - proceed with automatic read or write.
 *
 * > 0 - OK - read or write has been done by the strategy routine, so
 *       return immediately.
 *
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases -
 *
 * proc_dostring(), proc_dointvec(), proc_dointvec_jiffies(),
 * proc_dointvec_minmax(), proc_doulongvec_ms_jiffies_minmax(),
 * proc_doulongvec_minmax()
 *
 * It is the handler's job to read the input buffer from user memory
 * and process it. The handler should return 0 on success.
 *
 * This routine returns %NULL on a failure to register, and a pointer
 * to the table header on success.
 */
struct ctl_table_header *register_sysctl_table(ctl_table * table, 
					       int insert_at_head)
{
	struct ctl_table_header *tmp;
	tmp = kmalloc(sizeof(struct ctl_table_header), GFP_KERNEL);
	if (!tmp)
		return NULL;
	tmp->ctl_table = table;
	INIT_LIST_HEAD(&tmp->ctl_entry);
	if (insert_at_head)
		list_add(&tmp->ctl_entry, &root_table_header.ctl_entry);
	else
		list_add_tail(&tmp->ctl_entry, &root_table_header.ctl_entry);
#ifdef CONFIG_PROC_FS
	register_proc_table(table, proc_sys_root);
#endif
	return tmp;
}

/**
 * unregister_sysctl_table - unregister a sysctl table hierarchy
 * @header: the header returned from register_sysctl_table
 *
 * Unregisters the sysctl table and all children. proc entries may not
 * actually be removed until they are no longer used by anyone.
 */
void unregister_sysctl_table(struct ctl_table_header * header)
{
	list_del(&header->ctl_entry);
#ifdef CONFIG_PROC_FS
	unregister_proc_table(header->ctl_table, proc_sys_root);
#endif
	kfree(header);
}

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_FS

/* Scan the sysctl entries in table and add them all into /proc */
static void register_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int len;
	mode_t mode;
	
	for (; table->ctl_name; table++) {
		/* Can't do anything without a proc name. */
		if (!table->procname)
			continue;
		/* Maybe we can't do anything with it... */
		if (!table->proc_handler && !table->child) {
			printk(KERN_WARNING "SYSCTL: Can't register %s\n",
				table->procname);
			continue;
		}

		len = strlen(table->procname);
		mode = table->mode;

		de = NULL;
		if (table->proc_handler)
			mode |= S_IFREG;
		else {
			mode |= S_IFDIR;
			for (de = root->subdir; de; de = de->next) {
				if (proc_match(len, table->procname, de))
					break;
			}
			/* If the subdir exists already, de is non-NULL */
		}

		if (!de) {
			de = create_proc_entry(table->procname, mode, root);
			if (!de)
				continue;
			de->data = (void *) table;
			if (table->proc_handler) {
				de->proc_fops = &proc_sys_file_operations;
				de->proc_iops = &proc_sys_inode_operations;
			}
		}
		table->de = de;
		if (de->mode & S_IFDIR)
			register_proc_table(table->child, de);
	}
}

/*
 * Unregister a /proc sysctl table and any subdirectories.
 */
static void unregister_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	for (; table->ctl_name; table++) {
		if (!(de = table->de))
			continue;
		if (de->mode & S_IFDIR) {
			if (!table->child) {
				printk (KERN_ALERT "Help - malformed sysctl tree on free\n");
				continue;
			}
			unregister_proc_table(table->child, de);

			/* Don't unregister directories which still have entries.. */
			if (de->subdir)
				continue;
		}

		/* Don't unregister proc entries that are still being used.. */
		if (atomic_read(&de->count))
			continue;

		table->de = NULL;
		remove_proc_entry(table->procname, root);
	}
}

static ssize_t do_rw_proc(int write, struct file * file, char __user * buf,
			  size_t count, loff_t *ppos)
{
	int op;
	struct proc_dir_entry *de;
	struct ctl_table *table;
	size_t res;
	ssize_t error;
	
	de = PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		return -ENOTDIR;
	table = (struct ctl_table *) de->data;
	if (!table || !table->proc_handler)
		return -ENOTDIR;
	op = (write ? 002 : 004);
	if (ctl_perm(table, op))
		return -EPERM;
	
	res = count;

	/*
	 * FIXME: we need to pass on ppos to the handler.
	 */

	error = (*table->proc_handler) (table, write, file, buf, &res);
	if (error)
		return error;
	return res;
}

static ssize_t proc_readsys(struct file * file, char __user * buf,
			    size_t count, loff_t *ppos)
{
	return do_rw_proc(0, file, buf, count, ppos);
}

static ssize_t proc_writesys(struct file * file, const char __user * buf,
			     size_t count, loff_t *ppos)
{
	return do_rw_proc(1, file, (char __user *) buf, count, ppos);
}

static int proc_sys_permission(struct inode *inode, int op, struct nameidata *nd)
{
	return test_perm(inode->i_mode, op);
}

/**
 * proc_dostring - read a string sysctl
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes a string from/to the user buffer. If the kernel
 * buffer provided is not large enough to hold the string, the
 * string is truncated. The copied string is %NULL-terminated.
 * If the string is being read by the user process, it is copied
 * and a newline '\n' is added. It is truncated if the buffer is
 * not large enough.
 *
 * Returns 0 on success.
 */
int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp)
{
	size_t len;
	char __user *p;
	char c;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	if (write) {
		len = 0;
		p = buffer;
		while (len < *lenp) {
			if(get_user(c, p++))
				return -EFAULT;
			if (c == 0 || c == '\n')
				break;
			len++;
		}
		if (len >= table->maxlen)
			len = table->maxlen-1;
		if(copy_from_user(table->data, buffer, len))
			return -EFAULT;
		((char *) table->data)[len] = 0;
		filp->f_pos += *lenp;
	} else {
		len = strlen(table->data);
		if (len > table->maxlen)
			len = table->maxlen;
		if (len > *lenp)
			len = *lenp;
		if (len)
			if(copy_to_user(buffer, table->data, len))
				return -EFAULT;
		if (len < *lenp) {
			if(put_user('\n', ((char *) buffer) + len))
				return -EFAULT;
			len++;
		}
		*lenp = len;
		filp->f_pos += len;
	}
	return 0;
}

/*
 *	Special case of dostring for the UTS structure. This has locks
 *	to observe. Should this be in kernel/sys.c ????
 */
 
static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp)
{
	int r;

	if (!write) {
		down_read(&uts_sem);
		r=proc_dostring(table,0,filp,buffer,lenp);
		up_read(&uts_sem);
	} else {
		down_write(&uts_sem);
		r=proc_dostring(table,1,filp,buffer,lenp);
		up_write(&uts_sem);
	}
	return r;
}

#define OP_SET	0
#define OP_AND	1
#define OP_OR	2
#define OP_MAX	3
#define OP_MIN	4

static int do_proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, int conv, int op)
{
	int *i, vleft, first=1, neg, val;
	size_t left, len;
	
	#define TMPBUFLEN 20
	char buf[TMPBUFLEN], *p;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) table->data;
	vleft = table->maxlen / sizeof(int);
	left = *lenp;
	
	for (; left && vleft--; i++, first=0) {
		if (write) {
			while (left) {
				char c;
				if (get_user(c,(char __user *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				buffer++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0) * conv;
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			buffer += len;
			left -= len;
			switch(op) {
			case OP_SET:	*i = val; break;
			case OP_AND:	*i &= val; break;
			case OP_OR:	*i |= val; break;
			case OP_MAX:	if(*i < val)
						*i = val;
					break;
			case OP_MIN:	if(*i > val)
						*i = val;
					break;
			}
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%d", (*i) / conv);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 *
 * Returns 0 on success.
 */
int proc_dointvec(ctl_table *table, int write, struct file *filp,
		     void __user *buffer, size_t *lenp)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,1,OP_SET);
}

/*
 *	init may raise the set.
 */
 
int proc_dointvec_bset(ctl_table *table, int write, struct file *filp,
			void __user *buffer, size_t *lenp)
{
	if (!capable(CAP_SYS_MODULE)) {
		return -EPERM;
	}
	return do_proc_dointvec(table,write,filp,buffer,lenp,1,
				(current->pid == 1) ? OP_SET : OP_AND);
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp)
{
	int *i, *min, *max, vleft, first=1, neg, val;
	size_t len, left;
	#define TMPBUFLEN 20
	char buf[TMPBUFLEN], *p;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) table->data;
	min = (int *) table->extra1;
	max = (int *) table->extra2;
	vleft = table->maxlen / sizeof(int);
	left = *lenp;
	
	for (; left && vleft--; i++, min++, max++, first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c, (char *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				buffer++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0);
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			buffer += len;
			left -= len;

			if ((min && val < *min) || (max && val > *max))
				continue;
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%d", *i);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
}

static int do_proc_doulongvec_minmax(ctl_table *table, int write,
				     struct file *filp,
				     void __user *buffer, size_t *lenp,
				     unsigned long convmul,
				     unsigned long convdiv)
{
#define TMPBUFLEN 20
	unsigned long *i, *min, *max, val;
	int vleft, first=1, neg;
	size_t len, left;
	char buf[TMPBUFLEN], *p;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (unsigned long *) table->data;
	min = (unsigned long *) table->extra1;
	max = (unsigned long *) table->extra2;
	vleft = table->maxlen / sizeof(unsigned long);
	left = *lenp;
	
	for (; left && vleft--; i++, min++, max++, first=0) {
		if (write) {
			while (left) {
				char c;
				if (get_user(c, (char __user *) buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				buffer++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if (copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0) * convmul / convdiv ;
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			buffer += len;
			left -= len;

			if(neg)
				continue;
			if ((min && val < *min) || (max && val > *max))
				continue;
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%lu", convdiv * (*i) / convmul);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		p = (char *) buffer;
		while (left) {
			char c;
			if(get_user(c, p++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	filp->f_pos += *lenp;
	return 0;
#undef TMPBUFLEN
}

/**
 * proc_doulongvec_minmax - read a vector of long integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_minmax(ctl_table *table, int write, struct file *filp,
			   void __user *buffer, size_t *lenp)
{
    return do_proc_doulongvec_minmax(table, write, filp, buffer, lenp, 1l, 1l);
}

/**
 * proc_doulongvec_ms_jiffies_minmax - read a vector of millisecond values with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string. The values
 * are treated as milliseconds, and converted to jiffies when they are stored.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_ms_jiffies_minmax(ctl_table *table, int write,
				      struct file *filp,
				      void __user *buffer, size_t *lenp)
{
    return do_proc_doulongvec_minmax(table, write, filp, buffer,
				     lenp, HZ, 1000l);
}


/**
 * proc_dointvec_jiffies - read a vector of integers as seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 * The values read are assumed to be in seconds, and are converted into
 * jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
			  void __user *buffer, size_t *lenp)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,HZ,OP_SET);
}

#else /* CONFIG_PROC_FS */

int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

static int proc_doutsstring(ctl_table *table, int write, struct file *filp,
			    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_bset(ctl_table *table, int write, struct file *filp,
			void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(ctl_table *table, int write, struct file *filp,
		    void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_doulongvec_ms_jiffies_minmax(ctl_table *table, int write,
				      struct file *filp,
				      void *buffer, size_t *lenp)
{
    return -ENOSYS;
}


#endif /* CONFIG_PROC_FS */


/*
 * General sysctl support routines 
 */

/* The generic string strategy routine: */
int sysctl_string(ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen, void **context)
{
	size_t l, len;
	
	if (!table->data || !table->maxlen) 
		return -ENOTDIR;
	
	if (oldval && oldlenp) {
		if(get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			l = strlen(table->data);
			if (len > l) len = l;
			if (len >= table->maxlen)
				len = table->maxlen;
			if(copy_to_user(oldval, table->data, len))
				return -EFAULT;
			if(put_user(0, ((char *) oldval) + len))
				return -EFAULT;
			if(put_user(len, oldlenp))
				return -EFAULT;
		}
	}
	if (newval && newlen) {
		len = newlen;
		if (len > table->maxlen)
			len = table->maxlen;
		if(copy_from_user(table->data, newval, len))
			return -EFAULT;
		if (len == table->maxlen)
			len--;
		((char *) table->data)[len] = 0;
	}
	return 0;
}

/*
 * This function makes sure that all of the integers in the vector
 * are between the minimum and maximum values given in the arrays
 * table->extra1 and table->extra2, respectively.
 */
int sysctl_intvec(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen, void **context)
{
	int i, *vec, *min, *max;
	size_t length;

	if (newval && newlen) {
		if (newlen % sizeof(int) != 0)
			return -EINVAL;

		if (!table->extra1 && !table->extra2)
			return 0;

		if (newlen > table->maxlen)
			newlen = table->maxlen;
		length = newlen / sizeof(int);

		vec = (int *) newval;
		min = (int *) table->extra1;
		max = (int *) table->extra2;

		for (i = 0; i < length; i++) {
			int value;
			get_user(value, vec + i);
			if (min && value < min[i])
				return -EINVAL;
			if (max && value > max[i])
				return -EINVAL;
		}
	}
	return 0;
}

/* Strategy function to convert jiffies to seconds */ 
int sysctl_jiffies(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen, void **context)
{
	if (oldval) {
		size_t olen;
		if (oldlenp) { 
			if (get_user(olen, oldlenp))
				return -EFAULT;
			if (olen!=sizeof(int))
				return -EINVAL; 
		}
		if (put_user(*(int *)(table->data) / HZ, (int *)oldval) || 
		    (oldlenp && put_user(sizeof(int),oldlenp)))
			return -EFAULT;
	}
	if (newval && newlen) { 
		int new;
		if (newlen != sizeof(int))
			return -EINVAL; 
		if (get_user(new, (int *)newval))
			return -EFAULT;
		*(int *)(table->data) = new*HZ; 
	}
	return 1;
}


#else /* CONFIG_SYSCTL */


extern asmlinkage long sys_sysctl(struct __sysctl_args __user *args)
{
	return -ENOSYS;
}

int sysctl_string(ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen, void **context)
{
	return -ENOSYS;
}

int sysctl_intvec(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen, void **context)
{
	return -ENOSYS;
}

int sysctl_jiffies(ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen, void **context)
{
	return -ENOSYS;
}

int proc_dostring(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_bset(ctl_table *table, int write, struct file *filp,
			void __user *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
			  void *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp)
{
	return -ENOSYS;
}

int proc_doulongvec_ms_jiffies_minmax(ctl_table *table, int write,
				      struct file *filp,
				      void __user *buffer, size_t *lenp)
{
    return -ENOSYS;
}

struct ctl_table_header * register_sysctl_table(ctl_table * table, 
						int insert_at_head)
{
	return 0;
}

void unregister_sysctl_table(struct ctl_table_header * table)
{
}

#endif /* CONFIG_SYSCTL */
