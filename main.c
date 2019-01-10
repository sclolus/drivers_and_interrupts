// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/kdev_t.h>

MODULE_AUTHOR("sclolus");
MODULE_ALIAS("keyboard_driver");
MODULE_LICENSE("GPL v2");

#define MODULE_NAME "keyboard_driver"
#define LOG MODULE_NAME ": "
#define DEVICE_NBR_COUNT 2

static dev_t	major = (dev_t)42;
static int  __initdata test_param = 0;
module_param(test_param, int, 0);

static const struct usb_device_id usb_module_id_table[2] = {
	{ USB_INTERFACE_INFO(
			USB_INTERFACE_CLASS_HID,
			USB_INTERFACE_SUBCLASS_BOOT,
			USB_INTERFACE_PROTOCOL_KEYBOARD) },
	{}
};
MODULE_DEVICE_TABLE(usb, usb_module_id_table);

static int __init   init(void)
{
	int ret;
	int chrdev_result;

	ret = 0;

	if (test_param) {
		printk(KERN_INFO LOG "Test_param is %d\n", test_param);
	}


	if (major) {
		chrdev_result = register_chrdev_region(major, DEVICE_NBR_COUNT, MODULE_NAME);
	} else {
		chrdev_result = alloc_chrdev_region(&major, 0, DEVICE_NBR_COUNT, MODULE_NAME);
	}
	if (0 != chrdev_result) {
		printk(KERN_WARNING LOG "Failed to allocated device numbers\n");
		ret = -EPERM;
		goto out;
	}
out:
	return ret;
}
module_init(init);

static void __exit  cleanup(void)
{
	printk(KERN_INFO LOG "Cleanup up module\n");
	unregister_chrdev_region(major, DEVICE_NBR_COUNT);
}
module_exit(cleanup);
