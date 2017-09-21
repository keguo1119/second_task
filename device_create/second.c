
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>       /* struct file_operations, struct file */
#include <linux/miscdevice.h>  
#include <linux/uaccess.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#define SECOND_MAJOR 248
#define USE_MUTEX

static int second_major = SECOND_MAJOR;
module_param(second_major, int, S_IRUGO);
static struct class *second_class;

struct second_dev {
	struct cdev cdev;
#ifdef USE_MUTEX
	struct mutex mutex;
	unsigned int counter;
#else
	atomic_t counter;
#endif
	struct timer_list s_timer;
	struct device	*dev;
	const struct attribute_group	**groups;
	int state;
};

static int count = 0;

static struct second_dev *second_devp;

static void second_timer_handler(unsigned long arg)
{
	struct second_dev *dev = (void *) arg;
	mod_timer(&dev->s_timer, jiffies + HZ); /* 触发下一次定时 */
#ifdef  USE_MUTEX
	mutex_lock(&dev->mutex);
	dev->counter++;
	mutex_unlock(&dev->mutex);	
#else
	atomic_inc(&dev->counter); /* 增加秒计数 */
#endif

	printk(KERN_INFO "current jiffies is %ld\n", jiffies);
}

static int second_open(struct inode *inode, struct file *filp)
{
#ifdef  USE_MUTEX	
	mutex_lock(&second_devp->mutex);
    if(second_devp->state == 0)
    {
       setup_timer(&second_devp->s_timer, second_timer_handler, (unsigned long)second_devp);
       add_timer(&second_devp->s_timer);
       second_devp->state = 1;
    }
	mutex_unlock(&second_devp->mutex);
#else 
	if(second_devp->state == 0)
	{
	setup_timer(&second_devp->s_timer, second_timer_handler, (unsigned long)second_devp);
	add_timer(&second_devp->s_timer);
	second_devp->state = 1;
	}
	atomic_set(&second_devp->counter, 0);  /* 初始化秒计数为0 */   
#endif
 
//    filp->private_data = second_devp;
    printk(KERN_INFO "%s-open 1\n", __func__);
	return 0;
	
}

static int second_release(struct inode *inode, struct file *filp)
{
//	del_timer(&second_devp->s_timer);

	return 0;
}

static ssize_t second_read(struct file *filp, char __user * buf, size_t count,
	loff_t * ppos)
{
	int counter;
#ifdef  USE_MUTEX
	printk(KERN_INFO "%s mutex lock\n", __func__);
	mutex_lock(&second_devp->mutex);
	counter = second_devp->counter;
	mutex_unlock(&second_devp->mutex);
#else
	counter = atomic_read(&second_devp->counter);
#endif

	if (put_user(counter, (int *)buf)) /* 拷贝counter到userspace */
		return -EFAULT;
	else
		return sizeof(unsigned int);
}
////////////////////////////////////////////////////////////////////////////
ssize_t trigger_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct second_dev *sdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret = 0, status;

#ifdef  USE_MUTEX
	mutex_lock(&sdev->mutex);
	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	if (state == 1) {
		printk(KERN_INFO "%s: 1 - mutex lock, state = %ld\n", __func__, state);
		if (sdev->state == 0) {
			setup_timer(&sdev->s_timer, second_timer_handler, (unsigned long)sdev);
			add_timer(&sdev->s_timer);
			sdev->state = 1;
		}
	} else 	{
		printk(KERN_INFO "%s: 2 - mutex lock, state = %ld\n", __func__, state);
		if(sdev->state == 1) {
			del_timer(&sdev->s_timer);
			sdev->state = 0;
		}
	}
	printk(KERN_INFO "%s: 3 - mutex lock, state = %ld\n", __func__, state);

unlock:
	mutex_unlock(&sdev->mutex);
#endif
	printk(KERN_INFO "%s: 4  - mutex lock, count = %ld\n", __func__, count);
	return status ? : count;
////// 此处的返回值一定注意不要为0， 驱动程序会不停的从文件中读取数据
}

ssize_t trigger_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	int cnt = -1;

	struct second_dev *sdev = dev_get_drvdata(dev);

#ifndef  USE_MUTEX
	cnt = atomic_read(&sdev->counter);
#else
	printk(KERN_INFO "%s mutex lock\n", __func__);
	mutex_lock(&sdev->mutex);
	//    mutex_lock_interruptible(&second_devp->mutex);
	cnt = sdev->counter;
	mutex_unlock(&sdev->mutex);
#endif   

	return sprintf(buf, "second_trigger_show: count =%d\n", cnt);
//    return sprintf(buf, "second_trigger_show: count = %d, counter_addr=%p\n", *counter, counter);
}
//static DEVICE_ATTR_RW(trigger);
static DEVICE_ATTR(trigger, 0644, trigger_show, trigger_store);
static struct attribute *second_trigger_attrs[] = {
	&dev_attr_trigger.attr,	
	NULL,
};
static const struct attribute_group second_trigger_group = {
	.attrs = second_trigger_attrs,
};

static const struct attribute_group *second_groups[] = {
	&second_trigger_group,
	NULL,
};

///////////////////////////////////////////////////////////////////////////
static const struct file_operations second_fops = {
	.owner = THIS_MODULE,
	.open = second_open,
	.release = second_release,
	.read = second_read,
};

static void second_setup_cdev(struct second_dev *dev, int index)
{
	int err, devno = MKDEV(second_major, index);

	cdev_init(&dev->cdev, &second_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_ERR "Failed to add second device\n");
}
static struct miscdevice second_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "second",
    .fops = &second_fops
};

static int __init second_init(void)
{

	int ret, err;
	dev_t devno = MKDEV(SECOND_MAJOR, 0);
	printk(KERN_INFO "second init\n");
	second_devp = kzalloc(sizeof(*second_devp), GFP_KERNEL);
	if (!second_devp) {
		ret = -ENOMEM;
		return ret;
	}

#ifdef  USE_MUTEX
	printk(KERN_INFO "%s mutex lock\n", __func__);
	mutex_init(&second_devp->mutex);
	mutex_lock(&second_devp->mutex);
	second_devp->counter = 0;
	second_devp->state = 0;
	mutex_unlock(&second_devp->mutex); 
 #else
 	atomic_set(&second_devp->counter, 0);
 #endif

	cdev_init(&second_devp->cdev, &second_fops);
    second_devp->cdev.owner = THIS_MODULE;
	err = cdev_add(&second_devp->cdev, devno, 1);	
    if(err)
    {
         printk(KERN_DEBUG "Error %d adding second\n", err);
         return err;
	}
	printk(KERN_INFO "%s: 0 mutex lock\n", __func__);
	second_class = class_create(THIS_MODULE, "second");
    if (IS_ERR(second_class))
		return PTR_ERR(second_class);
	second_class->dev_groups = second_groups;

	printk(KERN_INFO "%s: 1 mutex lock\n", __func__);
//	second_devp->dev = device_create(second_class,  NULL,  
//		MKDEV(SECOND_MAJOR, 0), second_devp, "second");
	second_devp->dev =	device_create_with_groups(second_class,  NULL,  
			MKDEV(SECOND_MAJOR, 0), second_devp, second_devp->groups,"second");

	printk(KERN_INFO "second_dev drv proded\n");
	return 0;	
}
module_init(second_init);

static void __exit second_exit(void)
{
    if(second_devp)
    {
		cdev_del(&second_devp->cdev);   
		if(second_devp->state == 1)
			del_timer(&second_devp->s_timer);   
	}
	
	device_destroy(second_class, MKDEV(SECOND_MAJOR, 0));
	class_destroy(second_class);
}
module_exit(second_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2"); 
