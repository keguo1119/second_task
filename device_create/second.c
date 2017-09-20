
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

#define SECOND_MAJOR 248

static int second_major = SECOND_MAJOR;
module_param(second_major, int, S_IRUGO);
static struct class *second_class;

struct second_dev {
	struct cdev cdev;
	atomic_t counter;
	struct timer_list s_timer;
	struct device	*dev;
};

static struct second_dev *second_devp;

static void second_timer_handler(unsigned long arg)
{
	mod_timer(&second_devp->s_timer, jiffies + HZ); /* 触发下一次定时 */
	atomic_inc(&second_devp->counter); /* 增加秒计数 */

	printk(KERN_INFO "current jiffies is %ld\n", jiffies);
}

static int second_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "0-%s\n", __func__);
	init_timer(&second_devp->s_timer);
	second_devp->s_timer.function = &second_timer_handler;
	second_devp->s_timer.expires = jiffies + HZ;

	add_timer(&second_devp->s_timer);

	atomic_set(&second_devp->counter, 0);  /* 初始化秒计数为0 */                   
	printk(KERN_INFO "1-%s\n", __func__);
	return 0;
}

static int second_release(struct inode *inode, struct file *filp)
{
	del_timer(&second_devp->s_timer);

	return 0;
}

static ssize_t second_read(struct file *filp, char __user * buf, size_t count,
	loff_t * ppos)
{
	int counter;

	counter = atomic_read(&second_devp->counter);
	if (put_user(counter, (int *)buf)) /* 拷贝counter到userspace */
		return -EFAULT;
	else
		return sizeof(unsigned int);
}

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
	
    cdev_init(&second_devp->cdev, &second_fops);
    second_devp->cdev.owner = THIS_MODULE;
    err = cdev_add(&second_devp->cdev, devno, 1);
    if(err)
    {
         printk(KERN_DEBUG "Error %d adding second\n", err);
         return err;
	}
	
	second_class = class_create(THIS_MODULE, "second");
    if (IS_ERR(second_class))
		return PTR_ERR(second_class);
	
	second_devp->dev = device_create(second_class,  NULL,  
		MKDEV(SECOND_MAJOR, 0), second_devp, "second");
		
	printk(KERN_INFO "second_dev drv proded\n");
	return 0;	
}
module_init(second_init);

static void __exit second_exit(void)
{
    if(second_devp)
    {
        cdev_del(&second_devp->cdev);     
    }
	
	device_destroy(second_class, MKDEV(SECOND_MAJOR, 0));
	class_destroy(second_class);
}
module_exit(second_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2"); 
