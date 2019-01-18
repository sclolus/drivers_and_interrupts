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
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/seq_file.h>
#include "scan_code_sets.h"

MODULE_AUTHOR("sclolus");
MODULE_ALIAS("keyboard_driver");
MODULE_LICENSE("GPL v2");

#define PURE __attribute__((pure))

#define MODULE_NAME "keyboard_driver"
#define LOG MODULE_NAME ": "

#define KEYBOARD_IOPORT 0x60
#define PS2_DEFAULT_IRQ 1
#define DRIVER_DEFAULT_MINOR 42

static unsigned int	minor = 0;
static uint32_t		irq = 0;

DEFINE_MUTEX(open_mutex);

module_param(irq, uint, 0444);
module_param(minor, uint, 0444);

static const struct usb_device_id usb_module_id_table[2] = {
	{ USB_INTERFACE_INFO(
			USB_INTERFACE_CLASS_HID,
			USB_INTERFACE_SUBCLASS_BOOT,
			USB_INTERFACE_PROTOCOL_KEYBOARD) },
	{}
};
MODULE_DEVICE_TABLE(usb, usb_module_id_table);



static atomic_t	pending_data = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(read_wqueue);

static LIST_HEAD(key_entry_list);
static DEFINE_SPINLOCK(key_list_spinlock);
static DEFINE_SEMAPHORE(key_list_semaphore);


struct	driver_data {
	struct miscdevice   device;
	struct key_entry    *entries;
};

static struct driver_data  driver_data;

static int	driver_release(struct inode *inode, struct file *file);
static int	driver_open(struct inode *inode, struct file *file);
static const struct file_operations	device_fops = {
	.owner = THIS_MODULE,
	.open = &driver_open,
	.release = &driver_release,
	.read = &seq_read,
	.llseek = //&no_llseek,
	seq_lseek, //maybe ?
};

static void	*driver_seq_start(struct seq_file *seq_file, loff_t *pos);
static int	driver_seq_show(struct seq_file *seq_file, void *v);
static void	driver_seq_stop(struct seq_file *seq_file, void *v);
static void	*driver_seq_next(struct seq_file *seq_file, void *v, loff_t *pos);

static const struct seq_operations  seq_ops = {
	.start = driver_seq_start,
	.next  = driver_seq_next,
	.stop  = driver_seq_stop,
	.show  = driver_seq_show,
};

static PURE char	*ps2_key_state_to_string(enum ps2_key_state state) {
	switch (state) {
	case PRESSED:
		return "Pressed";
	case RELEASED:
		return "Released";
	default:
		printk(KERN_WARNING LOG "Invalid state was passed to key_state_to_string");
		return NULL;
	}
	return NULL;
}

static inline int32_t	    scan_code_set_entry_to_index(struct scan_key_code *set, uint64_t set_len, struct scan_key_code *entry)
{
	if (entry < set || entry > set + set_len) {
		return -ESRCH; //no such entry
	} else {
		return ((uint64_t)entry - (uint64_t)set) / sizeof(*set);
	}
}

static struct scan_key_code *find_scan_key_code(struct scan_key_code *set, uint64_t set_len, uint64_t code)
{
	uint64_t    i;

	i = 0;
	while (i < set_len) {
		if (code == set[i].code)
			return set + i;
		i++;
	}
	return NULL;
}

static bool		maybe_in_scan_set(struct scan_key_code *set, uint64_t set_len, uint64_t code, uint8_t current_index)
{
	uint64_t    i;

	i = 0;
	WARN_ON(current_index > sizeof(uint64_t));
	while (i < set_len) {
		if (code << current_index * 8U == (set[i].code & (0xFF << current_index * 8U)))
			return true;
		i++;
	}
	return false;
}

static irqreturn_t	keyboard_irq_handler(int irq, void *dev_id)
{
	uint64_t    		code;
	struct scan_key_code	*key_id;
	static bool		code_pending = false;
	static uint64_t		buffer_code = 0;
	static uint8_t		current_index = 0;

	atomic_set(&pending_data, 1);
	mb();
	code = inb(KEYBOARD_IOPORT);

	if (!maybe_in_scan_set(scan_code_set_1,
				sizeof(scan_code_set_1) / sizeof(*scan_code_set_1),
				code, current_index)) {
		printk(KERN_WARNING LOG "Dropping code: %llx, since it can't belong to the scan set table in use\n", code);
		code_pending = false;
		buffer_code = 0;
		current_index = 0;
		return IRQ_NONE;
	}

	if (code_pending) {
		code |= buffer_code << 8UL;
		current_index++;
	}

	key_id = find_scan_key_code(scan_code_set_1,
				sizeof(scan_code_set_1) / sizeof(*scan_code_set_1),
				(uint64_t)code);

	if (key_id == NULL) {
		code_pending = true;
		buffer_code <<= 8UL;
		buffer_code |= code;
		current_index++;
		printk(KERN_INFO LOG "Current buffered code: %#02llx\n", (uint64_t)buffer_code);
	} else {
		struct timeval	    now;
		long long	    hours;
		long long	    minutes;
		long long	    seconds;
		struct key_entry    *entry;

		if (NULL == (entry = kmalloc(sizeof(struct key_entry), GFP_ATOMIC))) {
			// Not much to do if kmalloc fails. just pop up a warning
			printk(KERN_WARNING LOG "Failed to allocated for key_entry, entry log will be lost\n");
			goto out;
		}
		do_gettimeofday(&now);

		entry->date = now;
		entry->index = scan_code_set_entry_to_index(scan_code_set_1,
				sizeof(scan_code_set_1) / sizeof(*scan_code_set_1),
							key_id);

		spin_lock(&key_list_spinlock);
		list_add_tail(&entry->head, &key_entry_list);
		printk(KERN_INFO LOG "In interrupt context: is_last: %d,  %px \n", (int)list_is_last(entry->head.prev, &key_entry_list), entry->head.prev);
		spin_unlock(&key_list_spinlock);
		hours = (now.tv_sec / 3600) % 24;
		now.tv_sec %= 3600;
		minutes = now.tv_sec / 60;
		now.tv_sec %= 60;
		seconds = now.tv_sec;

		printk(KERN_INFO LOG "%02lld:%02lld:%02lld %s(%#02llx) %s\n", hours, minutes, seconds, key_id->key_name, (uint64_t)code, ps2_key_state_to_string(key_id->state));

		code_pending = false;
		buffer_code = 0;
		current_index = 0;
		wake_up_interruptible(&read_wqueue);
	}
out:
	return IRQ_NONE;
}

static int  driver_register_irq(void *dev_id)
{
	WARN_ON(irq == 0);
	if (0 < request_irq(irq, &keyboard_irq_handler, IRQF_SHARED, MODULE_NAME, dev_id)) {
		printk(KERN_INFO LOG "Failed to request IRQ: %x\n", irq);
		return -EBUSY;
	}
	printk(KERN_INFO LOG "IRQ %d was registered by process: %s\n", irq, current->comm);
	return 0;
}

static int  driver_open(struct inode *inode, struct file *file)
{
	struct list_head *list = &key_entry_list;
	int		  ret;

	mutex_lock(&open_mutex);
	printk(KERN_INFO LOG "%s has opened the device\n", current->comm);

	file->private_data = NULL; //needed so that seq_open won't WARN_ON
	ret = seq_open(file, &seq_ops);
	if (ret) {
		printk(KERN_WARNING LOG "seq_open() failed\n");
		return ret;
	}
	while (list_is_last(list, &key_entry_list)) {
		printk(KERN_INFO LOG "In driver_open while(), list_is_last: %d, %px\n",(int)list_is_last(list, &key_entry_list), list);
		ret = wait_event_interruptible(read_wqueue, !list_is_last(list, &key_entry_list));
		printk(KERN_INFO LOG "woke up in open()\n");
		if (ret)
			return -ERESTARTSYS;

	}

	((struct seq_file *)file->private_data)->private = &key_entry_list;
	printk(KERN_INFO LOG "IN driver_open -> this is next: %px, this is head: %px\n", key_entry_list.next, &key_entry_list);
	return ret;
}

static void *driver_seq_start(struct seq_file *seq_file, loff_t *pos)
{
	struct list_head *list = seq_file->private;
	int		 ret;

	list = seq_list_start(&key_entry_list, *pos);

	printk(KERN_INFO LOG "*pos = %lld\n", *pos);
	if (list == NULL) {
		list =  key_entry_list.prev;
	}

	while (list_is_last(list, &key_entry_list)) {
		printk(KERN_INFO LOG "In driver_open while(), list_is_last: %d, %px\n",(int)list_is_last(list, &key_entry_list), list);
		ret = wait_event_interruptible(read_wqueue, !list_is_last(list, &key_entry_list));
		printk(KERN_INFO LOG "woke up in start()\n");
		if (ret)
			return (void *)-ERESTARTSYS;
	}


	printk(KERN_INFO LOG "In driver_seq_start()\n");
	//	unsigned long	flags;

//	spin_lock_irqsave(&key_list_spinlock, flags);

//	spin_lock_irqsave(&key_list_spinlock, flags);
	printk(KERN_INFO LOG "In start() ret = %px\n", list);
	return list;
}

static void driver_seq_stop(struct seq_file *seq_file, void *v)
{

}

static void *driver_seq_next(struct seq_file *seq_file, void *v, loff_t *pos)
{
	printk(KERN_INFO LOG "next() -> %px, *pos: %lld\n", v, *pos);
	if (list_is_last(v, &key_entry_list))
		return NULL;
	printk("next() list_is_last did not return\n");
	return seq_list_next(v, &key_entry_list, pos);

}

static int driver_seq_show(struct seq_file *seq_file, void *v)
{
	struct list_head	*list = v;
	struct key_entry        *key_entry;
	struct scan_key_code	*key_code;
	long long	    hours;
	long long	    minutes;
	long long	    seconds;
	unsigned long	    flags;

	printk(KERN_INFO LOG "In driver_seq_show()");
	if (list == NULL) {
		return -ESRCH; //dunno about this;
	}
	spin_lock_irqsave(&key_list_spinlock, flags);
	key_entry = list_entry(v, struct key_entry, head);
	hours = (key_entry->date.tv_sec / 3600) % 24;
	minutes = (key_entry->date.tv_sec / 60) % 60;
	seconds = (key_entry->date.tv_sec) % 60;

	key_code = &scan_code_set_1[key_entry->index]; //secure this
	spin_unlock_irqrestore(&key_list_spinlock, flags);
	seq_printf(seq_file, "%02lld:%02lld:%02lld %s(%#02llx) %s\n", hours, minutes, seconds,
		key_code->key_name,
		key_code->code,
		ps2_key_state_to_string(key_code->state));
	return 0;
}

static int  driver_release(struct inode *inode, struct file *file)
{
	struct list_head *cur;
	struct list_head *tmp;
	struct key_entry *key_entry;

	list_for_each_safe(cur, tmp, &key_entry_list) {
		key_entry = list_entry(cur, struct key_entry, head);
		WARN_ON(key_entry == NULL); //what the fuck I am doing this

		printk("Removing entry: %px\n", cur);

		list_del(cur);
		kfree(key_entry);
	}
	printk(KERN_INFO LOG "Release of " MODULE_NAME " file by pid: %d\n", current->tgid);
	mutex_unlock(&open_mutex);
	return 0;
}

static void __initdata	handle_params(void)
{
	if (irq != 0) {
		printk(KERN_INFO LOG "User requested default irq to be %d\n", irq);
	} else {
		irq = PS2_DEFAULT_IRQ;
	}

	if (minor != 0) {
		printk(KERN_INFO LOG "User request minor number for device to be %u\n", minor);
	} else {
		minor = DRIVER_DEFAULT_MINOR;
	}
}

static int __init	init(void)
{
	int		    ret;

	handle_params();
	ret = driver_register_irq(&key_entry_list);
	if (ret) {
		printk(KERN_WARNING LOG "Failed to register irq: %d\n", irq);
		return ret;
	}
	printk(KERN_INFO LOG "driver_register_irq() returned %d\n", ret);


	driver_data.device.name = MODULE_NAME;
	driver_data.device.fops = &device_fops;
	driver_data.device.parent = NULL; //check this
	driver_data.device.this_device = NULL; //check this
	driver_data.device.nodename = MODULE_NAME;
	driver_data.device.minor = minor;

	ret = misc_register(&driver_data.device);

	if (ret != 0) {
		printk(KERN_WARNING LOG "Failed to register misc device\n");
		goto out;
	}
out:
	return ret;
}
module_init(init);

static void __exit  cleanup(void)
{
	printk(KERN_INFO LOG "Cleanup up module\n");
	free_irq(irq, &key_entry_list);
	misc_deregister(&driver_data.device);
}
module_exit(cleanup);
