// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>

MODULE_AUTHOR("sclolus");
MODULE_ALIAS("keyboard_driver");
MODULE_LICENSE("GPL v2");

#define MODULE_NAME "keyboard_driver"
#define LOG MODULE_NAME ": "
#define DEVICE_NBR_COUNT 2

static DECLARE_WAIT_QUEUE_HEAD(keyboard_driver_queue);
static unsigned int	major = 42;
static unsigned int	minor = 0;
static dev_t		dev;
static struct cdev	device;

static int  __initdata	test_param = 0;
module_param(test_param, int, 0);

static const struct usb_device_id usb_module_id_table[2] = {
	{ USB_INTERFACE_INFO(
			USB_INTERFACE_CLASS_HID,
			USB_INTERFACE_SUBCLASS_BOOT,
			USB_INTERFACE_PROTOCOL_KEYBOARD) },
	{}
};
MODULE_DEVICE_TABLE(usb, usb_module_id_table);


static int  driver_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev;

	cdev = inode->i_cdev;
	file->private_data = cdev;
	nonseekable_open(inode, file);
	printk(KERN_INFO LOG "%s has opened the device\n", current->comm);
	return 0;
}

static int  driver_release(struct inode *inode, struct file *file)
{
	return 0;
}

static uint64_t    reader_count = 0;

static ssize_t	driver_read(struct file *file, char __user *to, size_t size, loff_t *off)
{
	ssize_t	    ret;
	uint64_t    len;

//	reader_count++;
	if (wait_event_interruptible(keyboard_driver_queue, reader_count == 2))
		return -ERESTARTSYS;
	if (*off >= sizeof(MODULE_NAME))
		return (0);
	printk(KERN_INFO "driver_read function has woken up, process->pid: %d\n", current->pid);
	len = min(size, sizeof(MODULE_NAME));
	ret = (ssize_t)copy_to_user(to, MODULE_NAME, len);
	if (ret != 0) {
		ret = -EPERM;
		goto out;
	}
	ret = (ssize_t)len;
	*off += len;
out:
	reader_count--;
	return ret;
}

static ssize_t driver_write(struct file *file, const char __user *_from, size_t size, loff_t *off)
{
	reader_count = 2;
	wake_up_interruptible(&keyboard_driver_queue);
	return 0;
}

static struct file_operations	device_fops = {
	.owner = THIS_MODULE,
	.open = &driver_open,
	.release = &driver_release,
	.read = &driver_read,
	.write = &driver_write,
	.llseek = &no_llseek,
};

static int __init   init(void)
{
	int	ret;
	int	chrdev_result;

	ret = 0;
	if (test_param) {
		printk(KERN_INFO LOG "Test_param is %d\n", test_param);
	}


	if (major) {
		dev = MKDEV(major, minor);
		chrdev_result = register_chrdev_region(dev, DEVICE_NBR_COUNT, MODULE_NAME);
	} else {
		chrdev_result = alloc_chrdev_region(&dev, 0, DEVICE_NBR_COUNT, MODULE_NAME);
	}
	if (0 != chrdev_result) {
		printk(KERN_WARNING LOG "Failed to allocated device numbers at major: %d\n", major);
		ret = chrdev_result;
		goto out;
	}

	cdev_init(&device, &device_fops);
	device.owner = THIS_MODULE;
	ret = cdev_add(&device, dev, 1);
	if (0 != ret) {
		printk(KERN_WARNING LOG "Failed to add character device\n");
		goto err_need_unregister_chrdev;
	}


	printk(KERN_INFO LOG "Registered dev: %d\n", dev);
err_need_unregister_chrdev:
	unregister_chrdev_region(major, DEVICE_NBR_COUNT);
out:
	return ret;
}
module_init(init);

static void __exit  cleanup(void)
{
	printk(KERN_INFO LOG "Cleanup up module\n");
	cdev_del(&device);
	unregister_chrdev_region(dev, DEVICE_NBR_COUNT);
}
module_exit(cleanup);
