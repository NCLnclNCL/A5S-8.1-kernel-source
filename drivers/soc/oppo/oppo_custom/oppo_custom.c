/************************************************************************************
 ** File: - kernel-4.4\drivers\soc\oppo\oppo_custom\oppo_custom.c
 ** VENDOR_EDIT
 ** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
 **
 ** Description:
 **      oppo_custom.c (sw23)
 **
 ** Version: 1.0
 ** Date created: 18:03:11,07/02/2017
 ** Author: Bin.Li@Prd.BaseDrv
 ** TAG: BSP.bootloader.bootflow
 ** --------------------------- Revision History: --------------------------------
 **  <author>      <data>            <desc>
 **  Bin.Li       2017/05/25        create the file
 **  Bin.Li       2017/05/27        Add for 6763 oppo_custom path
 ************************************************************************************/

#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>




#include <soc/oppo/oppo_custom.h>



/////////////////////////////////////////////////////////////

//#define OPPOCUSTOM_FILE "/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/oppo_custom"
//#define OPPOCUSTOM_FILE "/dev/block/platform/mtk-msdc.0/by-name/oppo_custom"
#define OPPOCUSTOM_FILE "/dev/block/platform/bootdevice/by-name/oppo_custom"
//#define OPPOCUSTOM_FILE "/dev/block/mmcblk0p7"

#define OPPOCUSTOM_SYNC_TIME 1000

struct oppocustom_data{
	struct delayed_work 		sync_work;
	//struct mutex 				wr_lock;
	int 						change_flag;
	int 						inited;
	unsigned int				tryTime;
	struct proc_dir_entry 		*proc_oppoCustom;
	TOppoCustConfigInf 			ConfigInf;
};

static struct oppocustom_data *gdata = NULL;

static int oppocustom_read(struct oppocustom_data *data)
{
    struct file* pfile = NULL;
	TOppoCustConfigInf *pConfigInf = &data->ConfigInf;
	loff_t pos = 0;
	mm_segment_t fs;
	int rc = -1;

	fs = get_fs();
	set_fs(KERNEL_DS);

	if(NULL == pfile){
		//pfile = filp_open(OPPOCUSTOM_FILE, O_RDONLY|O_TRUNC, 0);
		pfile = filp_open(OPPOCUSTOM_FILE, O_RDWR|O_TRUNC, 0);
	}
	if(IS_ERR(pfile)){
		printk("error occured while opening file %s %p\n",OPPOCUSTOM_FILE,pfile);
		set_fs(fs);
		rc = (int)IS_ERR(pfile);
		return rc;
	}
	//char * buf;
	rc = vfs_read(pfile, (void *)pConfigInf, sizeof(*pConfigInf), &pos);
	if(rc != sizeof(*pConfigInf))
	{
		filp_close(pfile, NULL);
		set_fs(fs);
		return rc;
	}
	filp_close(pfile, NULL);
	set_fs(fs);

	if(D_OPPO_CUST_PART_MAGIC_NUM != pConfigInf->nMagicNum1)
	{
		printk("oppocustom_read OPPO_CUSTOM partition is illegal!\n");
		rc = -2;
		return rc;
	}
	if(D_OPPO_CUST_PART_CONFIG_MAGIC_NUM != pConfigInf->nMagicNum2)
	{
		printk("oppocustom_read OPPO_CUSTOM partition with error config magic number!\n");
		rc = -3;
		return rc;
	}
	return 0;
}

static int oppocustom_write(struct oppocustom_data *data)
{
    struct file* pfile = NULL;
	TOppoCustConfigInf *pConfigInf = &data->ConfigInf;
	loff_t pos = 0;
	mm_segment_t fs;
	int rc = -1;

	if(D_OPPO_CUST_PART_MAGIC_NUM != pConfigInf->nMagicNum1)
	{
		printk("oppocustom_write magic num is illegal!\n");
		rc = -2;
		return rc;
	}
	if(D_OPPO_CUST_PART_CONFIG_MAGIC_NUM != pConfigInf->nMagicNum2)
	{
		printk("oppocustom_write magic num is illegal!\n");
		rc = -3;
		return rc;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	if(NULL == pfile){
		pfile = filp_open(OPPOCUSTOM_FILE, O_RDWR, 0);
	}
	if(IS_ERR(pfile)){
		printk("error occured while opening file %s\n",OPPOCUSTOM_FILE);
		set_fs(fs);
		return rc;
	}

	rc = vfs_write(pfile, (void *)pConfigInf, sizeof(*pConfigInf), &pos);
	if(rc != sizeof(*pConfigInf))
	{
		filp_close(pfile, NULL);
		set_fs(fs);
		return rc;
	}
	filp_close(pfile, NULL);
	set_fs(fs);
	return 0;
}

static void oppo_custome_sync_work(struct work_struct *work)
{
	int change_flag_old = 0;
	int rc = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oppocustom_data *data =
		container_of(dwork, struct oppocustom_data, sync_work);

	printk("oppo_custome_sync_work is called\n");
	if(!data->inited){
		if(data->tryTime > 100){
			printk("oppo_custome_sync_work:timeout tryTime = %d\n",data->tryTime);
			return;
		}
		if(sys_access(OPPOCUSTOM_FILE, 0) != 0){
			printk("oppo_custome_sync_work: file %s is no exit",OPPOCUSTOM_FILE);

			data->tryTime++;
			queue_delayed_work(system_power_efficient_wq, &data->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
			return;
		}
		rc = oppocustom_read(data);
		printk("oppo_custome_sync_work:rc = %d\n",rc);
		if(rc == 0){
			data->inited = 1;
		}else {
			data->tryTime++;
			queue_delayed_work(system_power_efficient_wq, &data->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
			return;
		}
	}
	if(data->change_flag > 0){
		change_flag_old = data->change_flag;
		oppocustom_write(data); //sync back
		data->change_flag -= change_flag_old;
		if(data->change_flag < 0){
			printk("oppo_custome_sync_work erro data->change_flag can not < 0 \n");
		}
	}
	if(data->change_flag > 0)
		queue_delayed_work(system_power_efficient_wq, &data->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
}

static int oppocustom_sync_init(void)
{
	int rc = -1;
	struct oppocustom_data *data = NULL;

	if(gdata){
		printk("%s:just can be call one time\n",__func__);
		return 0;
	}

	data = kzalloc(sizeof(struct oppocustom_data),GFP_KERNEL);
	if(data == NULL)
	{
		rc = -ENOMEM;
		printk("%s:kzalloc fail %d\n",__func__,rc);
		return rc;
	}
	//mutex_init(&data->wr_lock);
	INIT_DELAYED_WORK(&data->sync_work, oppo_custome_sync_work);
	gdata = data;
	queue_delayed_work(system_power_efficient_wq, &data->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
	return 0;
}

////////////////////////////////////////////////////////////

static ssize_t nPlUsbEnumEnabled_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	len = sprintf(page,"%d",gdata->ConfigInf.nPlUsbEnumEnabled);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static ssize_t nPlUsbEnumEnabled_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;

	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->ConfigInf.nPlUsbEnumEnabled){
		gdata->ConfigInf.nPlUsbEnumEnabled = input;
		gdata->change_flag++;
		if(gdata->change_flag == 1){
			queue_delayed_work(system_power_efficient_wq, &gdata->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
		}
	}

	return count;
}


static struct file_operations nPlUsbEnumEnabled_proc_fops = {
	.read = nPlUsbEnumEnabled_read_proc,
	.write = nPlUsbEnumEnabled_write_proc,
};

static ssize_t rpmb_enable_read_proc(struct file *file, char __user *buf, 
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}
	
	len = sprintf(page,"%d",gdata->ConfigInf.rpmb_enable == RPMB_ENABLE_MAGIC);

	if(len > *off)
		len -= *off;
	else
		len = 0;
	
	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t rpmb_enable_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}
	
	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;
 
	if (copy_from_user(page, buf, count))
		return -EFAULT;	
	*off += count;
	
	if (sscanf(page, "%u", &input) != 1) {
		count = -EINVAL;
		return count;
	}

	if(input != 0){
		gdata->ConfigInf.rpmb_enable = RPMB_ENABLE_MAGIC;
		gdata->change_flag++;
		if(gdata->change_flag == 1){
			queue_delayed_work(system_power_efficient_wq, &gdata->sync_work, 0);
		}
		//wait 1s for emmc write
		msleep(1000);
	}
	  
	return count;
}



static struct file_operations rpmb_enable_proc_fops = {
	.read = rpmb_enable_read_proc,
	.write = rpmb_enable_write_proc,
};

static ssize_t rpmb_key_provisioned_read_proc(struct file *file, char __user *buf, 
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	printk("rpmb key provisioned magic 0x%x\n", gdata->ConfigInf.rpmb_key_provisioned);
	
	len = sprintf(page,"%d",gdata->ConfigInf.rpmb_key_provisioned == RPMB_KEY_PROVISIONED);

	if(len > *off)
		len -= *off;
	else
		len = 0;
	
	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations rpmb_key_provisioned_proc_fops = {
	.read = rpmb_key_provisioned_read_proc,
	.write = NULL,
};

static ssize_t nUsbAutoSwitch_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	len = sprintf(page,"%d",gdata->ConfigInf.nUsbAutoSwitch);

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static ssize_t nUsbAutoSwitch_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	unsigned int input = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if (count > 256)
		count = 256;
	if(count > *off)
		count -= *off;
	else
		count = 0;

	if (copy_from_user(page, buf, count))
		return -EFAULT;
	*off += count;
	sscanf(page, "%u", &input);

	if (input != 1 && input != 10 && input != 0) {
		count = -EINVAL;
		return count;
	}

	if(input != gdata->ConfigInf.nUsbAutoSwitch){
		if (input == 10) { //bit1-3 use for auto enum bootrom control, if input==0xA, auto enum bootrom
			gdata->ConfigInf.nUsbAutoSwitch = (gdata->ConfigInf.nUsbAutoSwitch)|(input&0x0E);
		} else {
			gdata->ConfigInf.nUsbAutoSwitch = input;
		}
		printk("%d oppocustom_write nUsbAutoSwitch = 0x%x \n", __LINE__, gdata->ConfigInf.nUsbAutoSwitch);

		gdata->change_flag++;
		if(gdata->change_flag == 1){
			queue_delayed_work(system_power_efficient_wq, &gdata->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
		}
	}

	return count;
}


static struct file_operations nUsbAutoSwitch_proc_fops = {
	.read = nUsbAutoSwitch_read_proc,
	.write = nUsbAutoSwitch_write_proc,
};


static ssize_t Sensor_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if((SENSOR_BUF_SIZE - *off > 256) || (SENSOR_BUF_SIZE - *off < 0)){
		return len;
	}

	len = SENSOR_BUF_SIZE;

	if(len > *off)
		len -= *off;
	else{
		len = 0;
		return len;
	}
	if(SENSOR_BUF_SIZE - *off > 0){
		memcpy(page,&gdata->ConfigInf.Sensor[*off],SENSOR_BUF_SIZE - *off);
		if(copy_to_user(buf,page,(len < count ? len : count))){
			return -EFAULT;
		}
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static ssize_t Sensor_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	char page[256] = {0};
	int i;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if((SENSOR_BUF_SIZE - *off > 256) || (SENSOR_BUF_SIZE - *off < 0)){
		return 0;
	}

	if (count > 256)
		count = 256;

	if(count > *off)
		count -= *off;
	else{
		count = 0;
		return count;
	}

 	if( count > 0){
		if (copy_from_user(page, buf, count))
			return -EFAULT;
		for(i = *off;i < SENSOR_BUF_SIZE;i++){
			if(gdata->ConfigInf.Sensor[i] != page[i]){
				memcpy(&gdata->ConfigInf.Sensor[*off],page,count);
				gdata->change_flag++;
				if(gdata->change_flag == 1){
					queue_delayed_work(system_power_efficient_wq, &gdata->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
				}
				break;
			}
		}
		*off += count;
 	}

	return count;
}


static struct file_operations Sensor_proc_fops = {
	.read = Sensor_read_proc,
	.write = Sensor_write_proc,
};

static ssize_t DownloadTime_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if((DOWNLOADTIME_BUF_SIZE - *off > 256) || (DOWNLOADTIME_BUF_SIZE - *off < 0)){
		return len;
	}

	len = DOWNLOADTIME_BUF_SIZE;

	if(len > *off)
		len -= *off;
	else{
		len = 0;
		return len;
	}
	if(DOWNLOADTIME_BUF_SIZE - *off > 0){
		memcpy(page,&gdata->ConfigInf.DownloadTime[*off],DOWNLOADTIME_BUF_SIZE - *off);
		if(copy_to_user(buf,page,(len < count ? len : count))){
			return -EFAULT;
		}
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static struct file_operations DownloadTime_proc_fops = {
	.read = DownloadTime_read_proc,
	.write = NULL,
};
//oppo hufeng add begin
static ssize_t OppoCamera_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[1024] = {0};//set 1kb buf
	//int len = 0;
	printk("oppo_hufeng OppoCamera_read_proc count is %lu, off is %lld\n", count, *off);
	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if(count > 1024){
		count = 1024;
	}

	if(OPPOCAMERA_BUF_SIZE - *off < count){
		count = OPPOCAMERA_BUF_SIZE - *off;
	}
 
	if(count > 0){
		memcpy(page,&gdata->ConfigInf.OppoCamera[*off],count);
		if(copy_to_user(buf,page,count)){
			return -EFAULT;
		}
		*off += count;
	}
	return count;
}


static ssize_t OppoCamera_write_proc(struct file *file, const char __user *buf,
		size_t count, loff_t *off)

{
	unsigned char page[1024] = {0};
	//int i;
	//int len = count;
	printk("oppo_hufeng OppoCamera_write_proc count is %lu, off is %lld\n", count, *off);
	if(!gdata){
		return -ENOMEM;
	}

	if(!gdata->inited){
		return -ENOENT;
	}

	if(count > 1024){
		count = 1024;
	}

	if(OPPOCAMERA_BUF_SIZE - *off < count){
		count = OPPOCAMERA_BUF_SIZE - *off;
	}


 	if( count > 0){
		if (copy_from_user(page, buf, count))
			return -EFAULT;
		memcpy(&gdata->ConfigInf.OppoCamera[*off],page,count);
		gdata->change_flag++;
		if(gdata->change_flag == 1){
			queue_delayed_work(system_power_efficient_wq, &gdata->sync_work, msecs_to_jiffies(OPPOCUSTOM_SYNC_TIME));
		}
		*off += count;
 	}
	return count;
}


static struct file_operations OppoCamera_proc_fops = {
	.read = OppoCamera_read_proc,
	.write = OppoCamera_write_proc,
};
//oppo hufeng add end
static int __init oppocustom_init(void)
{
	int rc = 0;
	struct proc_dir_entry *pentry;

	rc = oppocustom_sync_init();
	if(rc < 0)
		return rc;



	if(gdata == NULL)
	{
		return rc;
	}

	if(gdata->proc_oppoCustom){
		printk("proc_oppoCustom has alread inited\n");
		return 0;
	}

	gdata->proc_oppoCustom = proc_mkdir("oppoCustom", NULL);
	if(!gdata->proc_oppoCustom) {
		pr_err("can't create oppoCustom proc\n");
		rc = -EFAULT;
		return rc;
	}
	pentry = proc_create("nPlUsbEnumEnabled",0664, gdata->proc_oppoCustom,
		&nPlUsbEnumEnabled_proc_fops);
	if(!pentry) {
		pr_err("create nPlUsbEnumEnabled proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
	pentry = proc_create("nUsbAutoSwitch", 0664, gdata->proc_oppoCustom,
		&nUsbAutoSwitch_proc_fops);
	if(!pentry) {
		pr_err("create nUsbAutoSwitch proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
	pentry = proc_create("Sensor", 0664, gdata->proc_oppoCustom,
		&Sensor_proc_fops);
	if(!pentry) {
		pr_err("create Sensor proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
	pentry = proc_create("DownloadTime", 0444, gdata->proc_oppoCustom,
		&DownloadTime_proc_fops);
	if(!pentry) {
		pr_err("create DownloadTime proc failed.\n");
		rc = -EFAULT;
		return rc;
	}

	pentry = proc_create("rpmb_enable", 0666, gdata->proc_oppoCustom, 
		&rpmb_enable_proc_fops);
	if(!pentry) {
		pr_err("create rpmb_enable proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
	
	pentry = proc_create("rpmb_key_provisioned", 0444, gdata->proc_oppoCustom, 
		&rpmb_key_provisioned_proc_fops);
	if(!pentry) {
		pr_err("create rpmb_key_provisioned proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
//oppo hufeng add begin
	pentry = proc_create("oppo_camera", 0664, gdata->proc_oppoCustom, 
		&OppoCamera_proc_fops);
	if(!pentry) {
		pr_err("create rpmb_key_provisioned proc failed.\n");
		rc = -EFAULT;
		return rc;
	}
//oppo hufeng add end
	return 0;

}



//module_init(oppocustom_init);

late_initcall(oppocustom_init);

MODULE_DESCRIPTION("OPPO custom version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joshua <gyx@oppo.com>");
