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

#include <linux/module.h>
#include <linux/aio.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/bitmap.h>
#include <linux/signal.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/ctype.h>
#include <linux/kmemcheck.h>
#include <linux/kmemleak.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/net.h>
#include <linux/sysrq.h>
#include <linux/highuid.h>
#include <linux/writeback.h>
#include <linux/ratelimit.h>
#include <linux/compaction.h>
#include <linux/hugetlb.h>
#include <linux/initrd.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/limits.h>
#include <linux/dcache.h>
#include <linux/dnotify.h>
#include <linux/syscalls.h>
#include <linux/vmstat.h>
#include <linux/nfs_fs.h>
#include <linux/acpi.h>
#include <linux/reboot.h>
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include <linux/pipe_fs_i.h>
#include <linux/oom.h>
#include <linux/kmod.h>
#include <linux/capability.h>
#include <linux/binfmts.h>
#include <linux/sched/sysctl.h>

#ifdef VENDOR_EDIT
//Ming.Liu@PSW.CN.WiFi.Network.quality.1065762, 2016/10/09
//add for: [monitor tcp info]
#include <net/tcp.h>
#endif /* VENDOR_EDIT */

#include <linux/kexec.h>
#include <linux/bpf.h>
#include <linux/mount.h>

#include <asm/uaccess.h>
#include <asm/processor.h>

#ifdef CONFIG_X86
#include <asm/nmi.h>
#include <asm/stacktrace.h>
#include <asm/io.h>
#endif
#ifdef CONFIG_SPARC
#include <asm/setup.h>
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
#include <linux/acct.h>
#endif
#ifdef CONFIG_RT_MUTEXES
#include <linux/rtmutex.h>
#endif
#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_LOCK_STAT)
#include <linux/lockdep.h>
#endif
#ifdef CONFIG_CHR_DEV_SG
#include <scsi/sg.h>
#endif

#ifdef CONFIG_LOCKUP_DETECTOR
#include <linux/nmi.h>
#endif

#if defined(CONFIG_SYSCTL)

/* External variables not in a header file. */
extern int suid_dumpable;
#ifdef CONFIG_COREDUMP
extern int core_uses_pid;
extern char core_pattern[];
extern unsigned int core_pipe_limit;
#endif
extern int pid_max;
extern int pid_max_min, pid_max_max;
extern int percpu_pagelist_fraction;
extern int latencytop_enabled;
extern unsigned int sysctl_nr_open_min, sysctl_nr_open_max;
#ifndef CONFIG_MMU
extern int sysctl_nr_trim_pages;
#endif

/* Constants used for minimum and  maximum */
#ifdef CONFIG_LOCKUP_DETECTOR
static int sixty = 60;
#endif

static int __maybe_unused neg_one = -1;

static int zero;
static int __maybe_unused one = 1;
static int __maybe_unused two = 2;
static int __maybe_unused four = 4;
static unsigned long one_ul = 1;
static int one_hundred = 100;
#ifdef VENDOR_EDIT //yixue.ge@PSW.BSP.Kernel.Driver 20170720 add for add direct_vm_swappiness
extern int direct_vm_swappiness;
static int two_hundred = 200;
#endif

static int one_thousand = 1000;

#ifdef VENDOR_EDIT
/*Huacai.Zhou@PSW.BSP.Kernel.Performance, 2018-04-28, add foreground task io opt*/
unsigned int sysctl_fg_io_opt = 1;
#endif /*VENDOR_EDIT*/

#ifdef CONFIG_PRINTK
static int ten_thousand = 10000;
#endif
#ifdef CONFIG_PERF_EVENTS
static int six_hundred_forty_kb = 640 * 1024;
#endif

/* this is needed for the proc_doulongvec_minmax of vm_dirty_bytes */
static unsigned long dirty_bytes_min = 2 * PAGE_SIZE;

/* this is needed for the proc_dointvec_minmax for [fs_]overflow UID and GID */
static int maxolduid = 65535;
static int minolduid;

static int ngroups_max = NGROUPS_MAX;
static const int cap_last_cap = CAP_LAST_CAP;

/*this is needed for proc_doulongvec_minmax of sysctl_hung_task_timeout_secs */
#ifdef CONFIG_DETECT_HUNG_TASK
static unsigned long hung_task_timeout_max = (LONG_MAX/HZ);
#endif

#ifdef CONFIG_INOTIFY_USER
#include <linux/inotify.h>
#endif
#ifdef CONFIG_SPARC
#endif

#ifdef __hppa__
extern int pwrsw_enabled;
#endif

#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_ALLOW
extern int unaligned_enabled;
#endif

#ifdef CONFIG_IA64
extern int unaligned_dump_stack;
#endif

#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_NO_WARN
extern int no_unaligned_warning;
#endif

#ifdef CONFIG_PROC_SYSCTL

#define SYSCTL_WRITES_LEGACY	-1
#define SYSCTL_WRITES_WARN	 0
#define SYSCTL_WRITES_STRICT	 1

static int sysctl_writes_strict = SYSCTL_WRITES_STRICT;

static int proc_do_cad_pid(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos);
static int proc_taint(struct ctl_table *table, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

#ifdef CONFIG_PRINTK
static int proc_dointvec_minmax_sysadmin(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

static int proc_dointvec_minmax_coredump(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos);
#ifdef CONFIG_COREDUMP
static int proc_dostring_coredump(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
/* Note: sysrq code uses it's own private copy */
static int __sysrq_enabled = CONFIG_MAGIC_SYSRQ_DEFAULT_ENABLE;

static int sysrq_sysctl_handler(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	int error;

	error = proc_dointvec(table, write, buffer, lenp, ppos);
	if (error)
		return error;

	if (write)
		sysrq_toggle_support(__sysrq_enabled);

	return 0;
}

#endif

static struct ctl_table kern_table[];
static struct ctl_table vm_table[];
static struct ctl_table fs_table[];
static struct ctl_table debug_table[];
static struct ctl_table dev_table[];
extern struct ctl_table random_table[];
#ifdef CONFIG_EPOLL
extern struct ctl_table epoll_table[];
#endif

#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
int sysctl_legacy_va_layout;
#endif

/* The default sysctl tables: */

static struct ctl_table sysctl_base_table[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= kern_table,
	},
	{
		.procname	= "vm",
		.mode		= 0555,
		.child		= vm_table,
	},
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{
		.procname	= "debug",
		.mode		= 0555,
		.child		= debug_table,
	},
	{
		.procname	= "dev",
		.mode		= 0555,
		.child		= dev_table,
	},
	{ }
};

#ifdef CONFIG_SCHED_DEBUG
static int min_sched_granularity_ns = 100000;		/* 100 usecs */
static int max_sched_granularity_ns = NSEC_PER_SEC;	/* 1 second */
static int min_wakeup_granularity_ns;			/* 0 usecs */
static int max_wakeup_granularity_ns = NSEC_PER_SEC;	/* 1 second */
#ifdef CONFIG_SMP
static int min_sched_tunable_scaling = SCHED_TUNABLESCALING_NONE;
static int max_sched_tunable_scaling = SCHED_TUNABLESCALING_END-1;
#endif /* CONFIG_SMP */
#endif /* CONFIG_SCHED_DEBUG */

#ifdef CONFIG_COMPACTION
static int min_extfrag_threshold;
static int max_extfrag_threshold = 1000;
#endif

static struct ctl_table kern_table[] = {
	{
		.procname	= "sched_child_runs_first",
		.data		= &sysctl_sched_child_runs_first,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SCHED_DEBUG
	{
		.procname	= "sched_min_granularity_ns",
		.data		= &sysctl_sched_min_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.procname	= "sched_latency_ns",
		.data		= &sysctl_sched_latency,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.procname	= "sched_sync_hint_enable",
		.data		= &sysctl_sched_sync_hint_enable,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname       = "sched_isolation_hint",
		.data           = &sysctl_sched_isolation_hint_enable,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
#ifdef CONFIG_SCHED_WALT
	{
		.procname	= "sched_use_walt_cpu_util",
		.data		= &sysctl_sched_use_walt_cpu_util,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef VENDOR_EDIT
/*Huacai.Zhou@PSW.BSP.Kernel.Performance, 2018-04-28, add foreground task io opt*/
{
		.procname	= "fg_io_opt",
		.data		= &sysctl_fg_io_opt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
},
#endif
	{
		.procname	= "sched_use_walt_task_util",
		.data		= &sysctl_sched_use_walt_task_util,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_walt_init_task_load_pct",
		.data		= &sysctl_sched_walt_init_task_load_pct,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_walt_cpu_high_irqload",
		.data		= &sysctl_sched_walt_cpu_high_irqload,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "sched_initial_task_util",
		.data		= &sysctl_sched_initial_task_util,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_cstate_aware",
		.data		= &sysctl_sched_cstate_aware,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_wakeup_granularity_ns",
		.data		= &sysctl_sched_wakeup_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_wakeup_granularity_ns,
		.extra2		= &max_wakeup_granularity_ns,
	},
#ifdef CONFIG_SMP
	{
		.procname	= "sched_tunable_scaling",
		.data		= &sysctl_sched_tunable_scaling,
		.maxlen		= sizeof(enum sched_tunable_scaling),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_tunable_scaling,
		.extra2		= &max_sched_tunable_scaling,
	},
	{
		.procname	= "sched_migration_cost_ns",
		.data		= &sysctl_sched_migration_cost,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_nr_migrate",
		.data		= &sysctl_sched_nr_migrate,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_time_avg_ms",
		.data		= &sysctl_sched_time_avg,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_shares_window_ns",
		.data		= &sysctl_sched_shares_window,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SCHEDSTATS
	{
		.procname	= "sched_schedstats",
		.data		= NULL,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sysctl_schedstats,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif /* CONFIG_SCHEDSTATS */
#endif /* CONFIG_SMP */
#ifdef CONFIG_NUMA_BALANCING
	{
		.procname	= "numa_balancing_scan_delay_ms",
		.data		= &sysctl_numa_balancing_scan_delay,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "numa_balancing_scan_period_min_ms",
		.data		= &sysctl_numa_balancing_scan_period_min,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "numa_balancing_scan_period_max_ms",
		.data		= &sysctl_numa_balancing_scan_period_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "numa_balancing_scan_size_mb",
		.data		= &sysctl_numa_balancing_scan_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
	},
	{
		.procname	= "numa_balancing",
		.data		= NULL, /* filled in by handler */
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sysctl_numa_balancing,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif /* CONFIG_NUMA_BALANCING */
#endif /* CONFIG_SCHED_DEBUG */
	{
		.procname	= "sched_rt_period_us",
		.data		= &sysctl_sched_rt_period,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_rt_handler,
	},
	{
		.procname	= "sched_rt_runtime_us",
		.data		= &sysctl_sched_rt_runtime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sched_rt_handler,
	},
	{
		.procname	= "sched_rr_timeslice_ms",
		.data		= &sched_rr_timeslice,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sched_rr_handler,
	},
#ifdef CONFIG_SCHED_AUTOGROUP
	{
		.procname	= "sched_autogroup_enabled",
		.data		= &sysctl_sched_autogroup_enabled,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_CFS_BANDWIDTH
	{
		.procname	= "sched_cfs_bandwidth_slice_us",
		.data		= &sysctl_sched_cfs_bandwidth_slice,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
	},
#endif
#ifdef CONFIG_SCHED_TUNE
	{
		.procname	= "sched_cfs_boost",
		.data		= &sysctl_sched_cfs_boost,
		.maxlen		= sizeof(sysctl_sched_cfs_boost),
#ifdef CONFIG_CGROUP_SCHEDTUNE
		.mode		= 0444,
#else
		.mode		= 0644,
#endif
		.proc_handler	= &sysctl_sched_cfs_boost_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#endif
#ifdef CONFIG_PROVE_LOCKING
	{
		.procname	= "prove_locking",
		.data		= &prove_locking,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_LOCK_STAT
	{
		.procname	= "lock_stat",
		.data		= &lock_stat,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "panic",
		.data		= &panic_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_COREDUMP
	{
		.procname	= "core_uses_pid",
		.data		= &core_uses_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "core_pattern",
		.data		= core_pattern,
		.maxlen		= CORENAME_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= proc_dostring_coredump,
	},
	{
		.procname	= "core_pipe_limit",
		.data		= &core_pipe_limit,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "tainted",
		.maxlen 	= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_taint,
	},
	{
		.procname	= "sysctl_writes_strict",
		.data		= &sysctl_writes_strict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &neg_one,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_LATENCYTOP
	{
		.procname	= "latencytop",
		.data		= &latencytop_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sysctl_latencytop,
	},
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	{
		.procname	= "real-root-dev",
		.data		= &real_root_dev,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "print-fatal-signals",
		.data		= &print_fatal_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SPARC
	{
		.procname	= "reboot-cmd",
		.data		= reboot_command,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "stop-a",
		.data		= &stop_a_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "scons-poweroff",
		.data		= &scons_pwroff,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_SPARC64
	{
		.procname	= "tsb-ratio",
		.data		= &sysctl_tsb_ratio,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef __hppa__
	{
		.procname	= "soft-power",
		.data		= &pwrsw_enabled,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_ALLOW
	{
		.procname	= "unaligned-trap",
		.data		= &unaligned_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "ctrl-alt-del",
		.data		= &C_A_D,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_FUNCTION_TRACER
	{
		.procname	= "ftrace_enabled",
		.data		= &ftrace_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= ftrace_enable_sysctl,
	},
#endif
#ifdef CONFIG_STACK_TRACER
	{
		.procname	= "stack_tracer_enabled",
		.data		= &stack_tracer_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= stack_trace_sysctl,
	},
#endif
#ifdef CONFIG_TRACING
	{
		.procname	= "ftrace_dump_on_oops",
		.data		= &ftrace_dump_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "traceoff_on_warning",
		.data		= &__disable_trace_on_warning,
		.maxlen		= sizeof(__disable_trace_on_warning),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tracepoint_printk",
		.data		= &tracepoint_printk,
		.maxlen		= sizeof(tracepoint_printk),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_KEXEC_CORE
	{
		.procname	= "kexec_load_disabled",
		.data		= &kexec_load_disabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		/* only handle a transition from default "0" to "1" */
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_MODULES
	{
		.procname	= "modprobe",
		.data		= &modprobe_path,
		.maxlen		= KMOD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "modules_disabled",
		.data		= &modules_disabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		/* only handle a transition from default "0" to "1" */
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_UEVENT_HELPER
	{
		.procname	= "hotplug",
		.data		= &uevent_helper,
		.maxlen		= UEVENT_HELPER_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
#endif
#ifdef CONFIG_CHR_DEV_SG
	{
		.procname	= "sg-big-buff",
		.data		= &sg_big_buff,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	{
		.procname	= "acct",
		.data		= &acct_parm,
		.maxlen		= 3*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_MAGIC_SYSRQ
	{
		.procname	= "sysrq",
		.data		= &__sysrq_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= sysrq_sysctl_handler,
	},
#endif
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "cad_pid",
		.data		= NULL,
		.maxlen		= sizeof (int),
		.mode		= 0600,
		.proc_handler	= proc_do_cad_pid,
	},
#endif
	{
		.procname	= "threads-max",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sysctl_max_threads,
	},
	{
		.procname	= "random",
		.mode		= 0555,
		.child		= random_table,
	},
	{
		.procname	= "usermodehelper",
		.mode		= 0555,
		.child		= usermodehelper_table,
	},
	{
		.procname	= "overflowuid",
		.data		= &overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.procname	= "overflowgid",
		.data		= &overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_S390
#ifdef CONFIG_MATHEMU
	{
		.procname	= "ieee_emulation_warnings",
		.data		= &sysctl_ieee_emulation_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "userprocess_debug",
		.data		= &show_unhandled_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "pid_max",
		.data		= &pid_max,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &pid_max_min,
		.extra2		= &pid_max_max,
	},
	{
		.procname	= "panic_on_oops",
		.data		= &panic_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#if defined CONFIG_PRINTK
	{
		.procname	= "printk",
		.data		= &console_loglevel,
		.maxlen		= 4*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_ratelimit",
		.data		= &printk_ratelimit_state.interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "printk_ratelimit_burst",
		.data		= &printk_ratelimit_state.burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_delay",
		.data		= &printk_delay_msec,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &ten_thousand,
	},
	{
		.procname	= "printk_devkmsg",
		.data		= devkmsg_log_str,
		.maxlen		= DEVKMSG_STR_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= devkmsg_sysctl_set_loglvl,
	},
	{
		.procname	= "dmesg_restrict",
		.data		= &dmesg_restrict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_sysadmin,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "kptr_restrict",
		.data		= &kptr_restrict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_sysadmin,
		.extra1		= &zero,
		.extra2		= &two,
	},
#endif
	{
		.procname	= "ngroups_max",
		.data		= &ngroups_max,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cap_last_cap",
		.data		= (void *)&cap_last_cap,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
#if defined(CONFIG_LOCKUP_DETECTOR)
	{
		.procname       = "watchdog",
		.data           = &watchdog_user_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_watchdog,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "watchdog_thresh",
		.data		= &watchdog_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_watchdog_thresh,
		.extra1		= &zero,
		.extra2		= &sixty,
	},
	{
		.procname       = "nmi_watchdog",
		.data           = &nmi_watchdog_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_nmi_watchdog,
		.extra1		= &zero,
#if defined(CONFIG_HAVE_NMI_WATCHDOG) || defined(CONFIG_HARDLOCKUP_DETECTOR)
		.extra2		= &one,
#else
		.extra2		= &zero,
#endif
	},
	{
		.procname       = "soft_watchdog",
		.data           = &soft_watchdog_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_soft_watchdog,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "watchdog_cpumask",
		.data		= &watchdog_cpumask_bits,
		.maxlen		= NR_CPUS,
		.mode		= 0644,
		.proc_handler	= proc_watchdog_cpumask,
	},
	{
		.procname	= "softlockup_panic",
		.data		= &softlockup_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#ifdef CONFIG_HARDLOCKUP_DETECTOR
	{
		.procname	= "hardlockup_panic",
		.data		= &hardlockup_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_SMP
	{
		.procname	= "softlockup_all_cpu_backtrace",
		.data		= &sysctl_softlockup_all_cpu_backtrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "hardlockup_all_cpu_backtrace",
		.data		= &sysctl_hardlockup_all_cpu_backtrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif /* CONFIG_SMP */
#endif
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86)
	{
		.procname       = "unknown_nmi_panic",
		.data           = &unknown_nmi_panic,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
#endif
#if defined(CONFIG_X86)
	{
		.procname	= "panic_on_unrecovered_nmi",
		.data		= &panic_on_unrecovered_nmi,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "panic_on_io_nmi",
		.data		= &panic_on_io_nmi,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	{
		.procname	= "panic_on_stackoverflow",
		.data		= &sysctl_panic_on_stackoverflow,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "bootloader_type",
		.data		= &bootloader_type,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bootloader_version",
		.data		= &bootloader_version,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "kstack_depth_to_print",
		.data		= &kstack_depth_to_print,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "io_delay_type",
		.data		= &io_delay_type,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if defined(CONFIG_MMU)
	{
		.procname	= "randomize_va_space",
		.data		= &randomize_va_space,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if defined(CONFIG_S390) && defined(CONFIG_SMP)
	{
		.procname	= "spin_retry",
		.data		= &spin_retry,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if	defined(CONFIG_ACPI_SLEEP) && defined(CONFIG_X86)
	{
		.procname	= "acpi_video_flags",
		.data		= &acpi_realmode_flags,
		.maxlen		= sizeof (unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif
#ifdef CONFIG_SYSCTL_ARCH_UNALIGN_NO_WARN
	{
		.procname	= "ignore-unaligned-usertrap",
		.data		= &no_unaligned_warning,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_IA64
	{
		.procname	= "unaligned-dump-stack",
		.data		= &unaligned_dump_stack,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_DETECT_HUNG_TASK
	{
		.procname	= "hung_task_panic",
		.data		= &sysctl_hung_task_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "hung_task_check_count",
		.data		= &sysctl_hung_task_check_count,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
	{
		.procname	= "hung_task_timeout_secs",
		.data		= &sysctl_hung_task_timeout_secs,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_dohung_task_timeout_secs,
		.extra2		= &hung_task_timeout_max,
	},
	{
		.procname	= "hung_task_warnings",
		.data		= &sysctl_hung_task_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &neg_one,
	},
#endif
#ifdef CONFIG_RT_MUTEXES
	{
		.procname	= "max_lock_depth",
		.data		= &max_lock_depth,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "poweroff_cmd",
		.data		= &poweroff_cmd,
		.maxlen		= POWEROFF_CMD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
#ifdef CONFIG_KEYS
	{
		.procname	= "keys",
		.mode		= 0555,
		.child		= key_sysctls,
	},
#endif
#ifdef CONFIG_PERF_EVENTS
	/*
	 * User-space scripts rely on the existence of this file
	 * as a feature check for perf_events being enabled.
	 *
	 * So it's an ABI, do not remove!
	 */
	{
		.procname	= "perf_event_paranoid",
		.data		= &sysctl_perf_event_paranoid,
		.maxlen		= sizeof(sysctl_perf_event_paranoid),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "perf_event_mlock_kb",
		.data		= &sysctl_perf_event_mlock,
		.maxlen		= sizeof(sysctl_perf_event_mlock),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "perf_event_max_sample_rate",
		.data		= &sysctl_perf_event_sample_rate,
		.maxlen		= sizeof(sysctl_perf_event_sample_rate),
		.mode		= 0644,
		.proc_handler	= perf_proc_update_handler,
		.extra1		= &one,
	},
	{
		.procname	= "perf_cpu_time_max_percent",
		.data		= &sysctl_perf_cpu_time_max_percent,
		.maxlen		= sizeof(sysctl_perf_cpu_time_max_percent),
		.mode		= 0644,
		.proc_handler	= perf_cpu_time_max_percent_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "perf_event_max_stack",
		.data		= &sysctl_perf_event_max_stack,
		.maxlen		= sizeof(sysctl_perf_event_max_stack),
		.mode		= 0644,
		.proc_handler	= perf_event_max_stack_handler,
		.extra1		= &zero,
		.extra2		= &six_hundred_forty_kb,
	},
	{
		.procname	= "perf_event_max_contexts_per_stack",
		.data		= &sysctl_perf_event_max_contexts_per_stack,
		.maxlen		= sizeof(sysctl_perf_event_max_contexts_per_stack),
		.mode		= 0644,
		.proc_handler	= perf_event_max_stack_handler,
		.extra1		= &zero,
		.extra2		= &one_thousand,
	},
#endif
#ifdef CONFIG_KMEMCHECK
	{
		.procname	= "kmemcheck",
		.data		= &kmemcheck_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "panic_on_warn",
		.data		= &panic_on_warn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
	{
		.procname	= "timer_migration",
		.data		= &sysctl_timer_migration,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= timer_migration_handler,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_BPF_SYSCALL
	{
		.procname	= "unprivileged_bpf_disabled",
		.data		= &sysctl_unprivileged_bpf_disabled,
		.maxlen		= sizeof(sysctl_unprivileged_bpf_disabled),
		.mode		= 0644,
		/* only handle a transition from default "0" to "1" */
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &one,
	},
#endif
#if defined(CONFIG_TREE_RCU) || defined(CONFIG_PREEMPT_RCU)
	{
		.procname	= "panic_on_rcu_stall",
		.data		= &sysctl_panic_on_rcu_stall,
		.maxlen		= sizeof(sysctl_panic_on_rcu_stall),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{ }
};

static struct ctl_table vm_table[] = {
	{
		.procname	= "overcommit_memory",
		.data		= &sysctl_overcommit_memory,
		.maxlen		= sizeof(sysctl_overcommit_memory),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &two,
	},
	{
		.procname	= "panic_on_oom",
		.data		= &sysctl_panic_on_oom,
		.maxlen		= sizeof(sysctl_panic_on_oom),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &two,
	},
	{
		.procname	= "oom_kill_allocating_task",
		.data		= &sysctl_oom_kill_allocating_task,
		.maxlen		= sizeof(sysctl_oom_kill_allocating_task),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oom_dump_tasks",
		.data		= &sysctl_oom_dump_tasks,
		.maxlen		= sizeof(sysctl_oom_dump_tasks),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "overcommit_ratio",
		.data		= &sysctl_overcommit_ratio,
		.maxlen		= sizeof(sysctl_overcommit_ratio),
		.mode		= 0644,
		.proc_handler	= overcommit_ratio_handler,
	},
	{
		.procname	= "overcommit_kbytes",
		.data		= &sysctl_overcommit_kbytes,
		.maxlen		= sizeof(sysctl_overcommit_kbytes),
		.mode		= 0644,
		.proc_handler	= overcommit_kbytes_handler,
	},
	{
		.procname	= "page-cluster",
		.data		= &page_cluster,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
	{
		.procname	= "dirty_background_ratio",
		.data		= &dirty_background_ratio,
		.maxlen		= sizeof(dirty_background_ratio),
		.mode		= 0644,
		.proc_handler	= dirty_background_ratio_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "dirty_background_bytes",
		.data		= &dirty_background_bytes,
		.maxlen		= sizeof(dirty_background_bytes),
		.mode		= 0644,
		.proc_handler	= dirty_background_bytes_handler,
		.extra1		= &one_ul,
	},
	{
		.procname	= "dirty_ratio",
		.data		= &vm_dirty_ratio,
		.maxlen		= sizeof(vm_dirty_ratio),
		.mode		= 0644,
		.proc_handler	= dirty_ratio_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "dirty_bytes",
		.data		= &vm_dirty_bytes,
		.maxlen		= sizeof(vm_dirty_bytes),
		.mode		= 0644,
		.proc_handler	= dirty_bytes_handler,
		.extra1		= &dirty_bytes_min,
	},
	{
		.procname	= "dirty_writeback_centisecs",
		.data		= &dirty_writeback_interval,
		.maxlen		= sizeof(dirty_writeback_interval),
		.mode		= 0644,
		.proc_handler	= dirty_writeback_centisecs_handler,
	},
	{
		.procname	= "dirty_expire_centisecs",
		.data		= &dirty_expire_interval,
		.maxlen		= sizeof(dirty_expire_interval),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
	{
		.procname	= "dirtytime_expire_seconds",
		.data		= &dirtytime_expire_interval,
		.maxlen		= sizeof(dirty_expire_interval),
		.mode		= 0644,
		.proc_handler	= dirtytime_interval_handler,
		.extra1		= &zero,
	},
	{
		.procname       = "nr_pdflush_threads",
		.mode           = 0444 /* read-only */,
		.proc_handler   = pdflush_proc_obsolete,
	},
	{
		.procname	= "swappiness",
		.data		= &vm_swappiness,
		.maxlen		= sizeof(vm_swappiness),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
#ifdef VENDOR_EDIT //yixue.ge@PSW.BSP.Kernel.Driver 20170720 add for add direct_vm_swappiness
		.extra2		= &two_hundred,
#else
		.extra2		= &one_hundred,
#endif
	},
#ifdef VENDOR_EDIT //yixue.ge@PSW.BSP.Kernel.Driver 20170720 add for add direct_vm_swappiness
	{
		.procname	= "direct_swappiness",
		.data		= &direct_vm_swappiness,
		.maxlen 	= sizeof(direct_vm_swappiness),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1 	= &zero,
		.extra2 	= &two_hundred,
	},
#endif
#ifdef CONFIG_HUGETLB_PAGE
	{
		.procname	= "nr_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_sysctl_handler,
	},
#ifdef CONFIG_NUMA
	{
		.procname       = "nr_hugepages_mempolicy",
		.data           = NULL,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &hugetlb_mempolicy_sysctl_handler,
	},
#endif
	 {
		.procname	= "hugetlb_shm_group",
		.data		= &sysctl_hugetlb_shm_group,
		.maxlen		= sizeof(gid_t),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	 },
	 {
		.procname	= "hugepages_treat_as_movable",
		.data		= &hugepages_treat_as_movable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nr_overcommit_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_overcommit_handler,
	},
#endif
	{
		.procname	= "lowmem_reserve_ratio",
		.data		= &sysctl_lowmem_reserve_ratio,
		.maxlen		= sizeof(sysctl_lowmem_reserve_ratio),
		.mode		= 0644,
		.proc_handler	= lowmem_reserve_ratio_sysctl_handler,
	},
	{
		.procname	= "drop_caches",
		.data		= &sysctl_drop_caches,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= drop_caches_sysctl_handler,
		.extra1		= &one,
		.extra2		= &four,
	},
#ifdef CONFIG_COMPACTION
	{
		.procname	= "compact_memory",
		.data		= &sysctl_compact_memory,
		.maxlen		= sizeof(int),
		.mode		= 0200,
		.proc_handler	= sysctl_compaction_handler,
	},
	{
		.procname	= "extfrag_threshold",
		.data		= &sysctl_extfrag_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sysctl_extfrag_handler,
		.extra1		= &min_extfrag_threshold,
		.extra2		= &max_extfrag_threshold,
	},
	{
		.procname	= "compact_unevictable_allowed",
		.data		= &sysctl_compact_unevictable_allowed,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
		.extra2		= &one,
	},

#endif /* CONFIG_COMPACTION */
	{
		.procname	= "min_free_kbytes",
		.data		= &min_free_kbytes,
		.maxlen		= sizeof(min_free_kbytes),
		.mode		= 0644,
		.proc_handler	= min_free_kbytes_sysctl_handler,
		.extra1		= &zero,
	},
	{
		.procname	= "watermark_scale_factor",
		.data		= &watermark_scale_factor,
		.maxlen		= sizeof(watermark_scale_factor),
		.mode		= 0644,
		.proc_handler	= watermark_scale_factor_sysctl_handler,
		.extra1		= &one,
		.extra2		= &one_thousand,
	},
	{
		.procname	= "percpu_pagelist_fraction",
		.data		= &percpu_pagelist_fraction,
		.maxlen		= sizeof(percpu_pagelist_fraction),
		.mode		= 0644,
		.proc_handler	= percpu_pagelist_fraction_sysctl_handler,
		.extra1		= &zero,
	},
#ifdef CONFIG_MMU
	{
		.procname	= "max_map_count",
		.data		= &sysctl_max_map_count,
		.maxlen		= sizeof(sysctl_max_map_count),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
#else
	{
		.procname	= "nr_trim_pages",
		.data		= &sysctl_nr_trim_pages,
		.maxlen		= sizeof(sysctl_nr_trim_pages),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
#endif
	{
		.procname	= "laptop_mode",
		.data		= &laptop_mode,
		.maxlen		= sizeof(laptop_mode),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "block_dump",
		.data		= &block_dump,
		.maxlen		= sizeof(block_dump),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
	{
		.procname	= "vfs_cache_pressure",
		.data		= &sysctl_vfs_cache_pressure,
		.maxlen		= sizeof(sysctl_vfs_cache_pressure),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
	{
		.procname	= "legacy_va_layout",
		.data		= &sysctl_legacy_va_layout,
		.maxlen		= sizeof(sysctl_legacy_va_layout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.procname	= "zone_reclaim_mode",
		.data		= &node_reclaim_mode,
		.maxlen		= sizeof(node_reclaim_mode),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
	{
		.procname	= "min_unmapped_ratio",
		.data		= &sysctl_min_unmapped_ratio,
		.maxlen		= sizeof(sysctl_min_unmapped_ratio),
		.mode		= 0644,
		.proc_handler	= sysctl_min_unmapped_ratio_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "min_slab_ratio",
		.data		= &sysctl_min_slab_ratio,
		.maxlen		= sizeof(sysctl_min_slab_ratio),
		.mode		= 0644,
		.proc_handler	= sysctl_min_slab_ratio_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#endif
#ifdef CONFIG_SMP
	{
		.procname	= "stat_interval",
		.data		= &sysctl_stat_interval,
		.maxlen		= sizeof(sysctl_stat_interval),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "stat_refresh",
		.data		= NULL,
		.maxlen		= 0,
		.mode		= 0600,
		.proc_handler	= vmstat_refresh,
	},
#endif
#ifdef CONFIG_MMU
	{
		.procname	= "mmap_min_addr",
		.data		= &dac_mmap_min_addr,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= mmap_min_addr_handler,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.procname	= "numa_zonelist_order",
		.data		= &numa_zonelist_order,
		.maxlen		= NUMA_ZONELIST_ORDER_LEN,
		.mode		= 0644,
		.proc_handler	= numa_zonelist_order_handler,
	},
#endif
#if (defined(CONFIG_X86_32) && !defined(CONFIG_UML))|| \
   (defined(CONFIG_SUPERH) && defined(CONFIG_VSYSCALL))
	{
		.procname	= "vdso_enabled",
#ifdef CONFIG_X86_32
		.data		= &vdso32_enabled,
		.maxlen		= sizeof(vdso32_enabled),
#else
		.data		= &vdso_enabled,
		.maxlen		= sizeof(vdso_enabled),
#endif
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#endif
#ifdef CONFIG_HIGHMEM
	{
		.procname	= "highmem_is_dirtyable",
		.data		= &vm_highmem_is_dirtyable,
		.maxlen		= sizeof(vm_highmem_is_dirtyable),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_MEMORY_FAILURE
	{
		.procname	= "memory_failure_early_kill",
		.data		= &sysctl_memory_failure_early_kill,
		.maxlen		= sizeof(sysctl_memory_failure_early_kill),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "memory_failure_recovery",
		.data		= &sysctl_memory_failure_recovery,
		.maxlen		= sizeof(sysctl_memory_failure_recovery),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{
		.procname	= "user_reserve_kbytes",
		.data		= &sysctl_user_reserve_kbytes,
		.maxlen		= sizeof(sysctl_user_reserve_kbytes),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "admin_reserve_kbytes",
		.data		= &sysctl_admin_reserve_kbytes,
		.maxlen		= sizeof(sysctl_admin_reserve_kbytes),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
	{
		.procname	= "mmap_rnd_bits",
		.data		= &mmap_rnd_bits,
		.maxlen		= sizeof(mmap_rnd_bits),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&mmap_rnd_bits_min,
		.extra2		= (void *)&mmap_rnd_bits_max,
	},
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
	{
		.procname	= "mmap_rnd_compat_bits",
		.data		= &mmap_rnd_compat_bits,
		.maxlen		= sizeof(mmap_rnd_compat_bits),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&mmap_rnd_compat_bits_min,
		.extra2		= (void *)&mmap_rnd_compat_bits_max,
	},
#endif
	{ }
};

static struct ctl_table fs_table[] = {
	{
		.procname	= "inode-nr",
		.data		= &inodes_stat,
		.maxlen		= 2*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
	{
		.procname	= "inode-state",
		.data		= &inodes_stat,
		.maxlen		= 7*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
	{
		.procname	= "file-nr",
		.data		= &files_stat,
		.maxlen		= sizeof(files_stat),
		.mode		= 0444,
		.proc_handler	= proc_nr_files,
	},
	{
		.procname	= "file-max",
		.data		= &files_stat.max_files,
		.maxlen		= sizeof(files_stat.max_files),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "nr_open",
		.data		= &sysctl_nr_open,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &sysctl_nr_open_min,
		.extra2		= &sysctl_nr_open_max,
	},
	{
		.procname	= "dentry-state",
		.data		= &dentry_stat,
		.maxlen		= 6*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_dentry,
	},
	{
		.procname	= "overflowuid",
		.data		= &fs_overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.procname	= "overflowgid",
		.data		= &fs_overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_FILE_LOCKING
	{
		.procname	= "leases-enable",
		.data		= &leases_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_DNOTIFY
	{
		.procname	= "dir-notify-enable",
		.data		= &dir_notify_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_MMU
#ifdef CONFIG_FILE_LOCKING
	{
		.procname	= "lease-break-time",
		.data		= &lease_break_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_AIO
	{
		.procname	= "aio-nr",
		.data		= &aio_nr,
		.maxlen		= sizeof(aio_nr),
		.mode		= 0444,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "aio-max-nr",
		.data		= &aio_max_nr,
		.maxlen		= sizeof(aio_max_nr),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif /* CONFIG_AIO */
#ifdef CONFIG_INOTIFY_USER
	{
		.procname	= "inotify",
		.mode		= 0555,
		.child		= inotify_table,
	},
#endif
#ifdef CONFIG_EPOLL
	{
		.procname	= "epoll",
		.mode		= 0555,
		.child		= epoll_table,
	},
#endif
#endif
	{
		.procname	= "protected_symlinks",
		.data		= &sysctl_protected_symlinks,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "protected_hardlinks",
		.data		= &sysctl_protected_hardlinks,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "protected_fifos",
		.data		= &sysctl_protected_fifos,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &two,
	},
	{
		.procname	= "protected_regular",
		.data		= &sysctl_protected_regular,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &two,
	},
	{
		.procname	= "suid_dumpable",
		.data		= &suid_dumpable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_coredump,
		.extra1		= &zero,
		.extra2		= &two,
	},
#if defined(CONFIG_BINFMT_MISC) || defined(CONFIG_BINFMT_MISC_MODULE)
	{
		.procname	= "binfmt_misc",
		.mode		= 0555,
		.child		= sysctl_mount_point,
	},
#endif
	{
		.procname	= "pipe-max-size",
		.data		= &pipe_max_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &pipe_proc_fn,
		.extra1		= &pipe_min_size,
	},
	{
		.procname	= "pipe-user-pages-hard",
		.data		= &pipe_user_pages_hard,
		.maxlen		= sizeof(pipe_user_pages_hard),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "pipe-user-pages-soft",
		.data		= &pipe_user_pages_soft,
		.maxlen		= sizeof(pipe_user_pages_soft),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "mount-max",
		.data		= &sysctl_mount_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
	},
	{ }
};

static struct ctl_table debug_table[] = {
#ifdef CONFIG_SYSCTL_EXCEPTION_TRACE
	{
		.procname	= "exception-trace",
		.data		= &show_unhandled_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif
#if defined(CONFIG_OPTPROBES)
	{
		.procname	= "kprobes-optimization",
		.data		= &sysctl_kprobes_optimization,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_kprobes_optimization_handler,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{ }
};

static struct ctl_table dev_table[] = {
	{ }
};

int __init sysctl_init(void)
{
	struct ctl_table_header *hdr;

	hdr = register_sysctl_table(sysctl_base_table);
	kmemleak_not_leak(hdr);
	return 0;
}

#endif /* CONFIG_SYSCTL */

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_SYSCTL

static int _proc_do_string(char *data, int maxlen, int write,
			   char __user *buffer,
			   size_t *lenp, loff_t *ppos)
{
	size_t len;
	char __user *p;
	char c;

	if (!data || !maxlen || !*lenp) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		if (sysctl_writes_strict == SYSCTL_WRITES_STRICT) {
			/* Only continue writes not past the end of buffer. */
			len = strlen(data);
			if (len > maxlen - 1)
				len = maxlen - 1;

			if (*ppos > len)
				return 0;
			len = *ppos;
		} else {
			/* Start writing from beginning of buffer. */
			len = 0;
		}

		*ppos += *lenp;
		p = buffer;
		while ((p - buffer) < *lenp && len < maxlen - 1) {
			if (get_user(c, p++))
				return -EFAULT;
			if (c == 0 || c == '\n')
				break;
			data[len++] = c;
		}
		data[len] = 0;
	} else {
		len = strlen(data);
		if (len > maxlen)
			len = maxlen;

		if (*ppos > len) {
			*lenp = 0;
			return 0;
		}

		data += *ppos;
		len  -= *ppos;

		if (len > *lenp)
			len = *lenp;
		if (len)
			if (copy_to_user(buffer, data, len))
				return -EFAULT;
		if (len < *lenp) {
			if (put_user('\n', buffer + len))
				return -EFAULT;
			len++;
		}
		*lenp = len;
		*ppos += len;
	}
	return 0;
}

static void warn_sysctl_write(struct ctl_table *table)
{
	pr_warn_once("%s wrote to %s when file position was not 0!\n"
		"This will not be supported in the future. To silence this\n"
		"warning, set kernel.sysctl_writes_strict = -1\n",
		current->comm, table->procname);
}

/**
 * proc_dostring - read a string sysctl
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
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
int proc_dostring(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	if (write && *ppos && sysctl_writes_strict == SYSCTL_WRITES_WARN)
		warn_sysctl_write(table);

	return _proc_do_string((char *)(table->data), table->maxlen, write,
			       (char __user *)buffer, lenp, ppos);
}

static size_t proc_skip_spaces(char **buf)
{
	size_t ret;
	char *tmp = skip_spaces(*buf);
	ret = tmp - *buf;
	*buf = tmp;
	return ret;
}

static void proc_skip_char(char **buf, size_t *size, const char v)
{
	while (*size) {
		if (**buf != v)
			break;
		(*size)--;
		(*buf)++;
	}
}

#define TMPBUFLEN 22
/**
 * proc_get_long - reads an ASCII formatted integer from a user buffer
 *
 * @buf: a kernel buffer
 * @size: size of the kernel buffer
 * @val: this is where the number will be stored
 * @neg: set to %TRUE if number is negative
 * @perm_tr: a vector which contains the allowed trailers
 * @perm_tr_len: size of the perm_tr vector
 * @tr: pointer to store the trailer character
 *
 * In case of success %0 is returned and @buf and @size are updated with
 * the amount of bytes read. If @tr is non-NULL and a trailing
 * character exists (size is non-zero after returning from this
 * function), @tr is updated with the trailing character.
 */
static int proc_get_long(char **buf, size_t *size,
			  unsigned long *val, bool *neg,
			  const char *perm_tr, unsigned perm_tr_len, char *tr)
{
	int len;
	char *p, tmp[TMPBUFLEN];

	if (!*size)
		return -EINVAL;

	len = *size;
	if (len > TMPBUFLEN - 1)
		len = TMPBUFLEN - 1;

	memcpy(tmp, *buf, len);

	tmp[len] = 0;
	p = tmp;
	if (*p == '-' && *size > 1) {
		*neg = true;
		p++;
	} else
		*neg = false;
	if (!isdigit(*p))
		return -EINVAL;

	*val = simple_strtoul(p, &p, 0);

	len = p - tmp;

	/* We don't know if the next char is whitespace thus we may accept
	 * invalid integers (e.g. 1234...a) or two integers instead of one
	 * (e.g. 123...1). So lets not allow such large numbers. */
	if (len == TMPBUFLEN - 1)
		return -EINVAL;

	if (len < *size && perm_tr_len && !memchr(perm_tr, *p, perm_tr_len))
		return -EINVAL;

	if (tr && (len < *size))
		*tr = *p;

	*buf += len;
	*size -= len;

	return 0;
}

/**
 * proc_put_long - converts an integer to a decimal ASCII formatted string
 *
 * @buf: the user buffer
 * @size: the size of the user buffer
 * @val: the integer to be converted
 * @neg: sign of the number, %TRUE for negative
 *
 * In case of success %0 is returned and @buf and @size are updated with
 * the amount of bytes written.
 */
static int proc_put_long(void __user **buf, size_t *size, unsigned long val,
			  bool neg)
{
	int len;
	char tmp[TMPBUFLEN], *p = tmp;

	sprintf(p, "%s%lu", neg ? "-" : "", val);
	len = strlen(tmp);
	if (len > *size)
		len = *size;
	if (copy_to_user(*buf, tmp, len))
		return -EFAULT;
	*size -= len;
	*buf += len;
	return 0;
}
#undef TMPBUFLEN

static int proc_put_char(void __user **buf, size_t *size, char c)
{
	if (*size) {
		char __user **buffer = (char __user **)buf;
		if (put_user(c, *buffer))
			return -EFAULT;
		(*size)--, (*buffer)++;
		*buf = *buffer;
	}
	return 0;
}

static int do_proc_dointvec_conv(bool *negp, unsigned long *lvalp,
				 int *valp,
				 int write, void *data)
{
	if (write) {
		if (*negp) {
			if (*lvalp > (unsigned long) INT_MAX + 1)
				return -EINVAL;
			*valp = -*lvalp;
		} else {
			if (*lvalp > (unsigned long) INT_MAX)
				return -EINVAL;
			*valp = *lvalp;
		}
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = true;
			*lvalp = -(unsigned long)val;
		} else {
			*negp = false;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

static int do_proc_douintvec_conv(bool *negp, unsigned long *lvalp,
				 int *valp,
				 int write, void *data)
{
	if (write) {
		if (*negp)
			return -EINVAL;
		if (*lvalp > UINT_MAX)
			return -EINVAL;
		*valp = *lvalp;
	} else {
		unsigned int val = *valp;
		*negp = false;
		*lvalp = (unsigned long)val;
	}
	return 0;
}

static const char proc_wspace_sep[] = { ' ', '\t', '\n' };

static int __do_proc_dointvec(void *tbl_data, struct ctl_table *table,
		  int write, void __user *buffer,
		  size_t *lenp, loff_t *ppos,
		  int (*conv)(bool *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
	int *i, vleft, first = 1, err = 0;
	size_t left;
	char *kbuf = NULL, *p;

	if (!tbl_data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	i = (int *) tbl_data;
	vleft = table->maxlen / sizeof(*i);
	left = *lenp;

	if (!conv)
		conv = do_proc_dointvec_conv;

	if (write) {
		if (*ppos) {
			switch (sysctl_writes_strict) {
			case SYSCTL_WRITES_STRICT:
				goto out;
			case SYSCTL_WRITES_WARN:
				warn_sysctl_write(table);
				break;
			default:
				break;
			}
		}

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		p = kbuf = memdup_user_nul(buffer, left);
		if (IS_ERR(kbuf))
			return PTR_ERR(kbuf);
	}

	for (; left && vleft--; i++, first=0) {
		unsigned long lval;
		bool neg;

		if (write) {
			left -= proc_skip_spaces(&p);

			if (!left)
				break;
			err = proc_get_long(&p, &left, &lval, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err)
				break;
			if (conv(&neg, &lval, i, 1, data)) {
				err = -EINVAL;
				break;
			}
		} else {
			if (conv(&neg, &lval, i, 0, data)) {
				err = -EINVAL;
				break;
			}
			if (!first)
				err = proc_put_char(&buffer, &left, '\t');
			if (err)
				break;
			err = proc_put_long(&buffer, &left, lval, neg);
			if (err)
				break;
		}
	}

	if (!write && !first && left && !err)
		err = proc_put_char(&buffer, &left, '\n');
	if (write && !err && left)
		left -= proc_skip_spaces(&p);
	if (write) {
		kfree(kbuf);
		if (first)
			return err ? : -EINVAL;
	}
	*lenp -= left;
out:
	*ppos += *lenp;
	return err;
}

static int do_proc_dointvec(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos,
		  int (*conv)(bool *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
	return __do_proc_dointvec(table->data, table, write,
			buffer, lenp, ppos, conv, data);
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * Returns 0 on success.
 */
int proc_dointvec(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, write, buffer, lenp, ppos, NULL, NULL);
}

#ifdef VENDOR_EDIT
//Ming.Liu@PSW.CN.WiFi.Network.quality.1065762, 2016/10/09,
//add for: [monitor tcp info]
static int proc_put_string(void __user **dst_buf, size_t *buf_size, char * src_str, int str_len)
{
	if (*buf_size >= str_len) {
		char __user **buffer = (char __user **)dst_buf;
		if ( copy_to_user(*buffer, src_str, str_len)) {
			printk(KERN_INFO "[proc_put_string] return -EFAULT; \n");
			return -EFAULT;
		}
		(*buf_size) -= str_len;
		(*buffer) += str_len;
		*dst_buf = *buffer;
		return 0;
	}

	return -EFAULT;
}

static int proc_string_memcpy(void **dst_buf, size_t *buf_size, char * src_str, int str_len)
{
	if (*buf_size >= str_len) {
		char **buffer = (char **)dst_buf;
		memcpy(*buffer, src_str, str_len);
		(*buf_size) -= str_len;
		(*buffer) += str_len;
		*dst_buf = *buffer;
		return 0;
	}

	return -EFAULT;
}



static int proc_put_one_tcpinfo(void **dst_buf, size_t *buf_size, struct tcp_info *info)
{
	char tmp[500];
	int len;



	len = sprintf(tmp, "state=%-10u ca_state=%-10u options=%-10u rto=%-10u ato=%-10u unacked=%-10u last_d_s=%-10u last_d_r=%-10u last_a_r=%-10u rtt=%-10u rcv_space=%-10u t_retrans=%-10u \n",
					info->tcpi_state, info->tcpi_ca_state, info->tcpi_options, info->tcpi_rto, info->tcpi_ato, info->tcpi_unacked,
					info->tcpi_last_data_sent, info->tcpi_last_data_recv, info->tcpi_last_ack_recv, info->tcpi_rtt, info->tcpi_rcv_space, info->tcpi_total_retrans);

	if (proc_string_memcpy(dst_buf, buf_size, tmp, len)) {
		return -EFAULT;
	}
	return 0;
}


int proc_do_print_tcpinfo(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	size_t left_count = *lenp;
	size_t temp_len = *lenp;
	void *mem_buff_ptr = NULL;
	void *temp_buff_ptr = NULL;
	int tcp_info_print = -1;

	if (table->data) {
		tcp_info_print = *((int *)table->data);
	}

	if ((tcp_info_print < 0) && (!table->data || !table->maxlen || !*lenp || (*ppos && !write))) {
		*lenp = 0;
		return 0;
	}


	if (! write && buffer) {
		struct tcp_info tcpinfo;
		unsigned int bucket;

		int print_counter = -1;

		mem_buff_ptr = kmalloc(left_count, GFP_ATOMIC);
		if (!mem_buff_ptr) {
			printk(KERN_INFO "[proc_do_tcpinfoprint] kmalloc is fail; mem_buff_ptr = NULL\n");
			goto put_return;
		}
		temp_buff_ptr = mem_buff_ptr;

		for (bucket = 0; bucket <= tcp_hashinfo.ehash_mask; bucket++) {
			struct sock *sk;
			struct hlist_nulls_node *node;
			spinlock_t *lock = inet_ehash_lockp(&tcp_hashinfo, bucket);
			spin_lock_bh(lock);

			sk_nulls_for_each(sk, node, &tcp_hashinfo.ehash[bucket].chain) {

				//Fix bug1102870: tcp_get_info cause APPS Crash
				sock_hold(sk);

				if ((sk->sk_state == TCP_TIME_WAIT) || (sk->sk_state == TCP_NEW_SYN_RECV)) {
					__sock_put(sk);
					continue;
				}

				print_counter ++;

				if (print_counter < tcp_info_print) {
					__sock_put(sk);
					continue;
				}


				tcp_get_info(sk, &tcpinfo);
				//Fix bug1102870: tcp_get_info cause APPS Crash
				__sock_put(sk);

				if (proc_put_one_tcpinfo(&temp_buff_ptr, &left_count, &tcpinfo)) {
					if (table->data) {  //save print tcpinfo sk index
					    *((int *)table->data) = print_counter;
					}
					spin_unlock_bh(lock);
					goto put_return;
				}

			}
			spin_unlock_bh(lock);
		}
		if (table->data) {  //recover tcp_info_print value
			*((int *)table->data) = -1;
		}

	}


put_return:

	if (mem_buff_ptr) {
		proc_put_string(&buffer, &temp_len, mem_buff_ptr, (*lenp) - left_count);
		kfree(mem_buff_ptr);
	}
	*lenp -= left_count;
	*ppos += left_count;



	return 0;
}
#endif /* VENDOR_EDIT */

/**
 * proc_douintvec - read a vector of unsigned integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) unsigned integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * Returns 0 on success.
 */
int proc_douintvec(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, write, buffer, lenp, ppos,
				do_proc_douintvec_conv, NULL);
}

/*
 * Taint values can only be increased
 * This means we can safely use a temporary.
 */
static int proc_taint(struct ctl_table *table, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	unsigned long tmptaint = get_taint();
	int err;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	t = *table;
	t.data = &tmptaint;
	err = proc_doulongvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;

	if (write) {
		/*
		 * Poor man's atomic or. Not worth adding a primitive
		 * to everyone's atomic.h for this
		 */
		int i;
		for (i = 0; i < BITS_PER_LONG && tmptaint >> i; i++) {
			if ((tmptaint >> i) & 1)
				add_taint(i, LOCKDEP_STILL_OK);
		}
	}

	return err;
}

#ifdef CONFIG_PRINTK
static int proc_dointvec_minmax_sysadmin(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

struct do_proc_dointvec_minmax_conv_param {
	int *min;
	int *max;
};

static int do_proc_dointvec_minmax_conv(bool *negp, unsigned long *lvalp,
					int *valp,
					int write, void *data)
{
	struct do_proc_dointvec_minmax_conv_param *param = data;
	if (write) {
		int val = *negp ? -*lvalp : *lvalp;
		if ((param->min && *param->min > val) ||
		    (param->max && *param->max < val))
			return -EINVAL;
		*valp = val;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = true;
			*lvalp = -(unsigned long)val;
		} else {
			*negp = false;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_dointvec_minmax(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct do_proc_dointvec_minmax_conv_param param = {
		.min = (int *) table->extra1,
		.max = (int *) table->extra2,
	};
	return do_proc_dointvec(table, write, buffer, lenp, ppos,
				do_proc_dointvec_minmax_conv, &param);
}

static void validate_coredump_safety(void)
{
#ifdef CONFIG_COREDUMP
	if (suid_dumpable == SUID_DUMP_ROOT &&
	    core_pattern[0] != '/' && core_pattern[0] != '|') {
		printk(KERN_WARNING "Unsafe core_pattern used with "\
			"suid_dumpable=2. Pipe handler or fully qualified "\
			"core dump path required.\n");
	}
#endif
}

static int proc_dointvec_minmax_coredump(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int error = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (!error)
		validate_coredump_safety();
	return error;
}

#ifdef CONFIG_COREDUMP
static int proc_dostring_coredump(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int error = proc_dostring(table, write, buffer, lenp, ppos);
	if (!error)
		validate_coredump_safety();
	return error;
}
#endif

static int __do_proc_doulongvec_minmax(void *data, struct ctl_table *table, int write,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	unsigned long *i, *min, *max;
	int vleft, first = 1, err = 0;
	size_t left;
	char *kbuf = NULL, *p;

	if (!data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	i = (unsigned long *) data;
	min = (unsigned long *) table->extra1;
	max = (unsigned long *) table->extra2;
	vleft = table->maxlen / sizeof(unsigned long);
	left = *lenp;

	if (write) {
		if (*ppos) {
			switch (sysctl_writes_strict) {
			case SYSCTL_WRITES_STRICT:
				goto out;
			case SYSCTL_WRITES_WARN:
				warn_sysctl_write(table);
				break;
			default:
				break;
			}
		}

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		p = kbuf = memdup_user_nul(buffer, left);
		if (IS_ERR(kbuf))
			return PTR_ERR(kbuf);
	}

	for (; left && vleft--; i++, first = 0) {
		unsigned long val;

		if (write) {
			bool neg;

			left -= proc_skip_spaces(&p);

			err = proc_get_long(&p, &left, &val, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err)
				break;
			if (neg)
				continue;
			val = convmul * val / convdiv;
			if ((min && val < *min) || (max && val > *max))
				continue;
			*i = val;
		} else {
			val = convdiv * (*i) / convmul;
			if (!first) {
				err = proc_put_char(&buffer, &left, '\t');
				if (err)
					break;
			}
			err = proc_put_long(&buffer, &left, val, false);
			if (err)
				break;
		}
	}

	if (!write && !first && left && !err)
		err = proc_put_char(&buffer, &left, '\n');
	if (write && !err)
		left -= proc_skip_spaces(&p);
	if (write) {
		kfree(kbuf);
		if (first)
			return err ? : -EINVAL;
	}
	*lenp -= left;
out:
	*ppos += *lenp;
	return err;
}

static int do_proc_doulongvec_minmax(struct ctl_table *table, int write,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	return __do_proc_doulongvec_minmax(table->data, table, write,
			buffer, lenp, ppos, convmul, convdiv);
}

/**
 * proc_doulongvec_minmax - read a vector of long integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_minmax(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, buffer, lenp, ppos, 1l, 1l);
}

/**
 * proc_doulongvec_ms_jiffies_minmax - read a vector of millisecond values with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
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
int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int write,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, buffer,
				     lenp, ppos, HZ, 1000l);
}


static int do_proc_dointvec_jiffies_conv(bool *negp, unsigned long *lvalp,
					 int *valp,
					 int write, void *data)
{
	if (write) {
		if (*lvalp > LONG_MAX / HZ)
			return 1;
		*valp = *negp ? -(*lvalp*HZ) : (*lvalp*HZ);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = -(unsigned long)val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = lval / HZ;
	}
	return 0;
}

static int do_proc_dointvec_userhz_jiffies_conv(bool *negp, unsigned long *lvalp,
						int *valp,
						int write, void *data)
{
	if (write) {
		if (USER_HZ < HZ && *lvalp > (LONG_MAX / HZ) * USER_HZ)
			return 1;
		*valp = clock_t_to_jiffies(*negp ? -*lvalp : *lvalp);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = -(unsigned long)val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = jiffies_to_clock_t(lval);
	}
	return 0;
}

static int do_proc_dointvec_ms_jiffies_conv(bool *negp, unsigned long *lvalp,
					    int *valp,
					    int write, void *data)
{
	if (write) {
		unsigned long jif = msecs_to_jiffies(*negp ? -*lvalp : *lvalp);

		if (jif > INT_MAX)
			return 1;
		*valp = (int)jif;
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = -(unsigned long)val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = jiffies_to_msecs(lval);
	}
	return 0;
}

/**
 * proc_dointvec_jiffies - read a vector of integers as seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in seconds, and are converted into
 * jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_jiffies(struct ctl_table *table, int write,
			  void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,buffer,lenp,ppos,
		    	    do_proc_dointvec_jiffies_conv,NULL);
}

/**
 * proc_dointvec_userhz_jiffies - read a vector of integers as 1/USER_HZ seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: pointer to the file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in 1/USER_HZ seconds, and
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write,
				 void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,buffer,lenp,ppos,
		    	    do_proc_dointvec_userhz_jiffies_conv,NULL);
}

/**
 * proc_dointvec_ms_jiffies - read a vector of integers as 1 milliseconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 * @ppos: the current position in the file
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in 1/1000 seconds, and
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_ms_jiffies(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, write, buffer, lenp, ppos,
				do_proc_dointvec_ms_jiffies_conv, NULL);
}

static int proc_do_cad_pid(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct pid *new_pid;
	pid_t tmp;
	int r;

	tmp = pid_vnr(cad_pid);

	r = __do_proc_dointvec(&tmp, table, write, buffer,
			       lenp, ppos, NULL, NULL);
	if (r || !write)
		return r;

	new_pid = find_get_pid(tmp);
	if (!new_pid)
		return -ESRCH;

	put_pid(xchg(&cad_pid, new_pid));
	return 0;
}

/**
 * proc_do_large_bitmap - read/write from/to a large bitmap
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * The bitmap is stored at table->data and the bitmap length (in bits)
 * in table->maxlen.
 *
 * We use a range comma separated format (e.g. 1,3-4,10-10) so that
 * large bitmaps may be represented in a compact manner. Writing into
 * the file will clear the bitmap then update it with the given input.
 *
 * Returns 0 on success.
 */
int proc_do_large_bitmap(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err = 0;
	bool first = 1;
	size_t left = *lenp;
	unsigned long bitmap_len = table->maxlen;
	unsigned long *bitmap = *(unsigned long **) table->data;
	unsigned long *tmp_bitmap = NULL;
	char tr_a[] = { '-', ',', '\n' }, tr_b[] = { ',', '\n', 0 }, c;

	if (!bitmap || !bitmap_len || !left || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		char *kbuf, *p;

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;

		p = kbuf = memdup_user_nul(buffer, left);
		if (IS_ERR(kbuf))
			return PTR_ERR(kbuf);

		tmp_bitmap = kzalloc(BITS_TO_LONGS(bitmap_len) * sizeof(unsigned long),
				     GFP_KERNEL);
		if (!tmp_bitmap) {
			kfree(kbuf);
			return -ENOMEM;
		}
		proc_skip_char(&p, &left, '\n');
		while (!err && left) {
			unsigned long val_a, val_b;
			bool neg;

			err = proc_get_long(&p, &left, &val_a, &neg, tr_a,
					     sizeof(tr_a), &c);
			if (err)
				break;
			if (val_a >= bitmap_len || neg) {
				err = -EINVAL;
				break;
			}

			val_b = val_a;
			if (left) {
				p++;
				left--;
			}

			if (c == '-') {
				err = proc_get_long(&p, &left, &val_b,
						     &neg, tr_b, sizeof(tr_b),
						     &c);
				if (err)
					break;
				if (val_b >= bitmap_len || neg ||
				    val_a > val_b) {
					err = -EINVAL;
					break;
				}
				if (left) {
					p++;
					left--;
				}
			}

			bitmap_set(tmp_bitmap, val_a, val_b - val_a + 1);
			first = 0;
			proc_skip_char(&p, &left, '\n');
		}
		kfree(kbuf);
	} else {
		unsigned long bit_a, bit_b = 0;

		while (left) {
			bit_a = find_next_bit(bitmap, bitmap_len, bit_b);
			if (bit_a >= bitmap_len)
				break;
			bit_b = find_next_zero_bit(bitmap, bitmap_len,
						   bit_a + 1) - 1;

			if (!first) {
				err = proc_put_char(&buffer, &left, ',');
				if (err)
					break;
			}
			err = proc_put_long(&buffer, &left, bit_a, false);
			if (err)
				break;
			if (bit_a != bit_b) {
				err = proc_put_char(&buffer, &left, '-');
				if (err)
					break;
				err = proc_put_long(&buffer, &left, bit_b, false);
				if (err)
					break;
			}

			first = 0; bit_b++;
		}
		if (!err)
			err = proc_put_char(&buffer, &left, '\n');
	}

	if (!err) {
		if (write) {
			if (*ppos)
				bitmap_or(bitmap, bitmap, tmp_bitmap, bitmap_len);
			else
				bitmap_copy(bitmap, tmp_bitmap, bitmap_len);
		}
		kfree(tmp_bitmap);
		*lenp -= left;
		*ppos += *lenp;
		return 0;
	} else {
		kfree(tmp_bitmap);
		return err;
	}
}

#else /* CONFIG_PROC_SYSCTL */

int proc_dostring(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_douintvec(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_ms_jiffies(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int write,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return -ENOSYS;
}


#endif /* CONFIG_PROC_SYSCTL */

/*
 * No sense putting this after each symbol definition, twice,
 * exception granted :-)
 */
EXPORT_SYMBOL(proc_dointvec);
EXPORT_SYMBOL(proc_douintvec);
EXPORT_SYMBOL(proc_dointvec_jiffies);
EXPORT_SYMBOL(proc_dointvec_minmax);
EXPORT_SYMBOL(proc_dointvec_userhz_jiffies);
EXPORT_SYMBOL(proc_dointvec_ms_jiffies);
EXPORT_SYMBOL(proc_dostring);
EXPORT_SYMBOL(proc_doulongvec_minmax);
EXPORT_SYMBOL(proc_doulongvec_ms_jiffies_minmax);
