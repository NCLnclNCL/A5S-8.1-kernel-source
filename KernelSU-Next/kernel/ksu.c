#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include "allowlist.h"
#include "arch.h"
#include "core_hook.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "throne_tracker.h"

#ifdef CONFIG_KSU_CMDLINE
#include <linux/init.h>

// use get_ksu_state()!
unsigned int enable_kernelsu = 1; // enabled by default
static int __init read_kernelsu_state(char *s)
{
	if (s)
		enable_kernelsu = simple_strtoul(s, NULL, 0);
	return 1;
}
__setup("kernelsu.enabled=", read_kernelsu_state);

bool get_ksu_state(void) { return enable_kernelsu >= 1; }
#else
bool get_ksu_state(void) { return true; }
#endif /* CONFIG_KSU_CMDLINE */

#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs.h>
#endif

static struct workqueue_struct *ksu_workqueue;

bool ksu_queue_work(struct work_struct *work)
{
	return queue_work(ksu_workqueue, work);
}

extern int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr,
					void *argv, void *envp, int *flags);

extern int ksu_handle_execveat_ksud(int *fd, struct filename **filename_ptr,
				    void *argv, void *envp, int *flags);

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv,
			void *envp, int *flags)
{
	ksu_handle_execveat_ksud(fd, filename_ptr, argv, envp, flags);
	return ksu_handle_execveat_sucompat(fd, filename_ptr, argv, envp,
					    flags);
}

extern void ksu_sucompat_init();
extern void ksu_sucompat_exit();
extern void ksu_ksud_init();
extern void ksu_ksud_exit();

int __init ksu_kernelsu_init(void)
{
	pr_info("kernelsu.enabled=%d\n",
		(int)get_ksu_state());

#ifdef CONFIG_KSU_CMDLINE
	if (!get_ksu_state()) {
		pr_info_once("drivers is disabled.");
		return 0;
	}
#endif

#ifdef CONFIG_KSU_DEBUG
	pr_alert("*************************************************************");
	pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
	pr_alert("**                                                         **");
	pr_alert("**         You are running KernelSU in DEBUG mode          **");
	pr_alert("**                                                         **");
	pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
	pr_alert("*************************************************************");
#endif

#ifdef CONFIG_KSU_SUSFS
	susfs_init();
#endif

	ksu_core_init();

	ksu_workqueue = alloc_ordered_workqueue("kernelsu_work_queue", 0);

	ksu_allowlist_init();

	ksu_throne_tracker_init();

	ksu_sucompat_init();

#ifdef CONFIG_KSU_KPROBES_HOOK
	ksu_ksud_init();
#else
	pr_debug("init ksu driver\n");
#endif

#ifdef MODULE
#ifndef CONFIG_KSU_DEBUG
	kobject_del(&THIS_MODULE->mkobj.kobj);
#endif
#endif
	return 0;
}

void ksu_kernelsu_exit(void)
{
#ifdef CONFIG_KSU_CMDLINE
	if (!get_ksu_state()) {
		return;
	}
#endif
	ksu_allowlist_exit();

	ksu_throne_tracker_exit();

	destroy_workqueue(ksu_workqueue);

#ifdef CONFIG_KSU_KPROBES_HOOK
	ksu_ksud_exit();
#endif
	ksu_sucompat_exit();

	ksu_core_exit();
}

module_init(ksu_kernelsu_init);
module_exit(ksu_kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU");

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
