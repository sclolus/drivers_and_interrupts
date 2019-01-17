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

MODULE_AUTHOR("sclolus");
MODULE_ALIAS("keyboard_driver");
MODULE_LICENSE("GPL v2");

#define PURE __attribute__((pure))

#define MODULE_NAME "keyboard_driver"
#define LOG MODULE_NAME ": "
#define DEVICE_NBR_COUNT 2
#define KEYBOARD_IOPORT 0x60

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

enum	ps2_key_state {
	PRESSED,
	RELEASED
};

struct scan_key_code {
	uint64_t	code;
	char	        *key_name;
	enum ps2_key_state	state;
};

struct	key_entry {
	// index inside the scan code set
	uint32_t		index;

	// Data at which the entry was performed
	struct timeval		date;

	struct list_head	head;
};

struct	driver_data {
	struct miscdevice   device;
	struct key_entry    *entries;
};

static struct driver_data  driver_data;

static int	driver_release(struct inode *inode, struct file *file);
/* static ssize_t	driver_read(struct file *file, char __user *to, size_t size, loff_t *off); */
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


struct scan_key_code	scan_code_set_1[] = {
	{ 0x1, "escape", PRESSED },
	{ 0x2, "1", PRESSED },
	{ 0x3, "2", PRESSED },
	{ 0x4, "3", PRESSED },
	{ 0x5, "4", PRESSED },
	{ 0x6, "5", PRESSED },
	{ 0x7, "6", PRESSED },
	{ 0x8, "7", PRESSED },
	{ 0x9, "8", PRESSED },
	{ 0xa, "9", PRESSED },
	{ 0xb, "0 (zero)", PRESSED },
	{ 0xc, "-", PRESSED },
	{ 0xd, "=", PRESSED },
	{ 0xe, "backspace", PRESSED },
	{ 0xf, "tab", PRESSED },
	{ 0x10, "Q", PRESSED },
	{ 0x11, "W", PRESSED },
	{ 0x12, "E", PRESSED },
	{ 0x13, "R", PRESSED },
	{ 0x14, "T", PRESSED },
	{ 0x15, "Y", PRESSED },
	{ 0x16, "U", PRESSED },
	{ 0x17, "I", PRESSED },
	{ 0x18, "O", PRESSED },
	{ 0x19, "P", PRESSED },
	{ 0x1a, "[", PRESSED },
	{ 0x1b, "]", PRESSED },
	{ 0x1c, "enter", PRESSED },
	{ 0x1d, "left control", PRESSED },
	{ 0x1e, "A", PRESSED },
	{ 0x1f, "S", PRESSED },
	{ 0x20, "D", PRESSED },
	{ 0x21, "F", PRESSED },
	{ 0x22, "G", PRESSED },
	{ 0x23, "H", PRESSED },
	{ 0x24, "J", PRESSED },
	{ 0x25, "K", PRESSED },
	{ 0x26, "L", PRESSED },
	{ 0x27, ";", PRESSED },
	{ 0x28, "' (single quote)", PRESSED },
	{ 0x29, "` (back tick)", PRESSED },
	{ 0x2a, "left shift", PRESSED },
	{ 0x2b, "\\", PRESSED },
	{ 0x2c, "Z", PRESSED },
	{ 0x2d, "X", PRESSED },
	{ 0x2e, "C", PRESSED },
	{ 0x2f, "V", PRESSED },
	{ 0x30, "B", PRESSED },
	{ 0x31, "N", PRESSED },
	{ 0x32, "M", PRESSED },
	{ 0x33, ",", PRESSED },
	{ 0x34, ".", PRESSED },
	{ 0x35, "/", PRESSED },
	{ 0x36, "right shift", PRESSED },
	{ 0x37, "(keypad) *", PRESSED },
	{ 0x38, "left alt", PRESSED },
	{ 0x39, "space", PRESSED },
	{ 0x3a, "CapsLock", PRESSED },
	{ 0x3b, "F1", PRESSED },
	{ 0x3c, "F2", PRESSED },
	{ 0x3d, "F3", PRESSED },
	{ 0x3e, "F4", PRESSED },
	{ 0x3f, "F5", PRESSED },
	{ 0x40, "F6", PRESSED },
	{ 0x41, "F7", PRESSED },
	{ 0x42, "F8", PRESSED },
	{ 0x43, "F9", PRESSED },
	{ 0x44, "F10", PRESSED },
	{ 0x45, "NumberLock", PRESSED },
	{ 0x46, "ScrollLock", PRESSED },
	{ 0x47, "(keypad) 7", PRESSED },
	{ 0x48, "(keypad) 8", PRESSED },
	{ 0x49, "(keypad) 9", PRESSED },
	{ 0x4a, "(keypad) -", PRESSED },
	{ 0x4b, "(keypad) 4", PRESSED },
	{ 0x4c, "(keypad) 5", PRESSED },
	{ 0x4d, "(keypad) 6", PRESSED },
	{ 0x4e, "(keypad) +", PRESSED },
	{ 0x4f, "(keypad) 1", PRESSED },
	{ 0x50, "(keypad) 2", PRESSED },
	{ 0x51, "(keypad) 3", PRESSED },
	{ 0x52, "(keypad) 0", PRESSED },
	{ 0x53, "(keypad) .", PRESSED },
	{ 0x57, "F11", PRESSED },
	{ 0x58, "F12", PRESSED },
	{ 0x81, "escape", RELEASED },
	{ 0x82, "1", RELEASED },
	{ 0x83, "2", RELEASED },
	{ 0x84, "3", RELEASED },
	{ 0x85, "4", RELEASED },
	{ 0x86, "5", RELEASED },
	{ 0x87, "6", RELEASED },
	{ 0x88, "7", RELEASED },
	{ 0x89, "8", RELEASED },
	{ 0x8a, "9", RELEASED },
	{ 0x8b, "0 (zero)", RELEASED },
	{ 0x8c, "-", RELEASED },
	{ 0x8d, "=", RELEASED },
	{ 0x8e, "backspace", RELEASED },
	{ 0x8f, "tab", RELEASED },
	{ 0x90, "Q", RELEASED },
	{ 0x91, "W", RELEASED },
	{ 0x92, "E", RELEASED },
	{ 0x93, "R", RELEASED },
	{ 0x94, "T", RELEASED },
	{ 0x95, "Y", RELEASED },
	{ 0x96, "U", RELEASED },
	{ 0x97, "I", RELEASED },
	{ 0x98, "O", RELEASED },
	{ 0x99, "P", RELEASED },
	{ 0x9a, "[", RELEASED },
	{ 0x9b, "]", RELEASED },
	{ 0x9c, "enter", RELEASED },
	{ 0x9d, "left control", RELEASED },
	{ 0x9e, "A", RELEASED },
	{ 0x9f, "S", RELEASED },
	{ 0xa0, "D", RELEASED },
	{ 0xa1, "F", RELEASED },
	{ 0xa2, "G", RELEASED },
	{ 0xa3, "H", RELEASED },
	{ 0xa4, "J", RELEASED },
	{ 0xa5, "K", RELEASED },
	{ 0xa6, "L", RELEASED },
	{ 0xa7, ";", RELEASED },
	{ 0xa8, "' (single quote)", RELEASED },
	{ 0xa9, "` (back tick)", RELEASED },
	{ 0xaa, "left shift", RELEASED },
	{ 0xab, "\\", RELEASED },
	{ 0xac, "Z", RELEASED },
	{ 0xad, "X", RELEASED },
	{ 0xae, "C", RELEASED },
	{ 0xaf, "V", RELEASED },
	{ 0xb0, "B", RELEASED },
	{ 0xb1, "N", RELEASED },
	{ 0xb2, "M", RELEASED },
	{ 0xb3, ",", RELEASED },
	{ 0xb4, ".", RELEASED },
	{ 0xb5, "/", RELEASED },
	{ 0xb6, "right shift", RELEASED },
	{ 0xb7, "(keypad) *", RELEASED },
	{ 0xb8, "left alt", RELEASED },
	{ 0xb9, "space", RELEASED },
	{ 0xba, "CapsLock", RELEASED },
	{ 0xbb, "F1", RELEASED },
	{ 0xbc, "F2", RELEASED },
	{ 0xbd, "F3", RELEASED },
	{ 0xbe, "F4", RELEASED },
	{ 0xbf, "F5", RELEASED },
	{ 0xc0, "F6", RELEASED },
	{ 0xc1, "F7", RELEASED },
	{ 0xc2, "F8", RELEASED },
	{ 0xc3, "F9", RELEASED },
	{ 0xc4, "F10", RELEASED },
	{ 0xc5, "NumberLock", RELEASED },
	{ 0xc6, "ScrollLock", RELEASED },
	{ 0xc7, "(keypad) 7", RELEASED },
	{ 0xc8, "(keypad) 8", RELEASED },
	{ 0xc9, "(keypad) 9", RELEASED },
	{ 0xca, "(keypad) -", RELEASED },
	{ 0xcb, "(keypad) 4", RELEASED },
	{ 0xcc, "(keypad) 5", RELEASED },
	{ 0xcd, "(keypad) 6", RELEASED },
	{ 0xce, "(keypad) +", RELEASED },
	{ 0xcf, "(keypad) 1", RELEASED },
	{ 0xd0, "(keypad) 2", RELEASED },
	{ 0xd1, "(keypad) 3", RELEASED },
	{ 0xd2, "(keypad) 0", RELEASED },
	{ 0xd3, "(keypad) .", RELEASED },
	{ 0xd7, "F11", RELEASED },
	{ 0xd8, "F12", RELEASED },
	{ 0xe010, "(multimedia) previous track", PRESSED },
	{ 0xe019, "(multimedia) next track", PRESSED },
	{ 0xe01c, "(keypad) enter", PRESSED },
	{ 0xe01d, "right control", PRESSED },
	{ 0xe020, "(multimedia) mute", PRESSED },
	{ 0xe021, "(multimedia) calculator", PRESSED },
	{ 0xe022, "(multimedia) play", PRESSED },
	{ 0xe024, "(multimedia) stop", PRESSED },
	{ 0xe02e, "(multimedia) volume down", PRESSED },
	{ 0xe030, "(multimedia) volume up", PRESSED },
	{ 0xe032, "(multimedia) WWW home", PRESSED },
	{ 0xe035, "(keypad) /", PRESSED },
	{ 0xe038, "right alt (or altGr)", PRESSED },
	{ 0xe047, "home", PRESSED },
	{ 0xe048, "cursor up", PRESSED },
	{ 0xe049, "page up", PRESSED },
	{ 0xe04b, "cursor left", PRESSED },
	{ 0xe04d, "cursor right", PRESSED },
	{ 0xe04f, "end", PRESSED },
	{ 0xe050, "cursor down", PRESSED },
	{ 0xe051, "page down", PRESSED },
	{ 0xe052, "insert", PRESSED },
	{ 0xe053, "delete", PRESSED },
	{ 0xe05b, "left GUI", PRESSED },
	{ 0xe05c, "right GUI", PRESSED },
	{ 0xe05d, "\"apps\"", PRESSED },
	{ 0xe05e, "(ACPI) power", PRESSED },
	{ 0xe05f, "(ACPI) sleep", PRESSED },
	{ 0xe063, "(ACPI) wake", PRESSED },
	{ 0xe065, "(multimedia) WWW search", PRESSED },
	{ 0xe066, "(multimedia) WWW favorites", PRESSED },
	{ 0xe067, "(multimedia) WWW refresh", PRESSED },
	{ 0xe068, "(multimedia) WWW stop", PRESSED },
	{ 0xe069, "(multimedia) WWW forward", PRESSED },
	{ 0xe06a, "(multimedia) WWW back", PRESSED },
	{ 0xe06b, "(multimedia) my computer", PRESSED },
	{ 0xe06c, "(multimedia) email", PRESSED },
	{ 0xe06d, "(multimedia) media select", PRESSED },
	{ 0xe090, "(multimedia) previous track", RELEASED },
	{ 0xe099, "(multimedia) next track", RELEASED },
	{ 0xe09c, "(keypad) enter", RELEASED },
	{ 0xe09d, "right control", RELEASED },
	{ 0xe0a0, "(multimedia) mute", RELEASED },
	{ 0xe0a1, "(multimedia) calculator", RELEASED },
	{ 0xe0a2, "(multimedia) play", RELEASED },
	{ 0xe0a4, "(multimedia) stop", RELEASED },
	{ 0xe0ae, "(multimedia) volume down", RELEASED },
	{ 0xe0b0, "(multimedia) volume up", RELEASED },
	{ 0xe0b2, "(multimedia) WWW home", RELEASED },
	{ 0xe0b5, "(keypad) /", RELEASED },
	{ 0xe0b8, "right alt (or altGr)", RELEASED },
	{ 0xe0c7, "home", RELEASED },
	{ 0xe0c8, "cursor up", RELEASED },
	{ 0xe0c9, "page up", RELEASED },
	{ 0xe0cb, "cursor left", RELEASED },
	{ 0xe0cd, "cursor right", RELEASED },
	{ 0xe0cf, "end", RELEASED },
	{ 0xe0d0, "cursor down", RELEASED },
	{ 0xe0d1, "page down", RELEASED },
	{ 0xe0d2, "insert", RELEASED },
	{ 0xe0d3, "delete", RELEASED },
	{ 0xe0db, "left GUI", RELEASED },
	{ 0xe0dc, "right GUI", RELEASED },
	{ 0xe0dd, "\"apps\"", RELEASED },
	{ 0xe0de, "(ACPI) power", RELEASED },
	{ 0xe0df, "(ACPI) sleep", RELEASED },
	{ 0xe0e3, "(ACPI) wake", RELEASED },
	{ 0xe0e5, "(multimedia) WWW search", RELEASED },
	{ 0xe0e6, "(multimedia) WWW favorites", RELEASED },
	{ 0xe0e7, "(multimedia) WWW refresh", RELEASED },
	{ 0xe0e8, "(multimedia) WWW stop", RELEASED },
	{ 0xe0e9, "(multimedia) WWW forward", RELEASED },
	{ 0xe0ea, "(multimedia) WWW back", RELEASED },
	{ 0xe0eb, "(multimedia) my computer", RELEASED },
	{ 0xe0ec, "(multimedia) email", RELEASED },
	{ 0xe0ed, "(multimedia) media select", RELEASED },
	{ 0xe02ae037, "print screen", PRESSED },
	{ 0xe0b7e0aa, "print screen", RELEASED },
	{ 0xe11d45e19dc5, "pause", PRESSED },
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


static irqreturn_t	keyboard_irq_handler(int irq, void *dev_id)
{
	uint64_t    		code;
	struct scan_key_code	*key_id;
	static bool		code_pending = false;
	static uint64_t		buffer_code = 0;

	atomic_set(&pending_data, 1);
	mb();
	code = inb(KEYBOARD_IOPORT);

	if (code_pending) {
		code |= buffer_code << 8UL;
	}

	key_id = find_scan_key_code(scan_code_set_1,
				sizeof(scan_code_set_1) / sizeof(*scan_code_set_1),
				(uint64_t)code);

	if (key_id == NULL) {
		code_pending = true;
		buffer_code <<= 8UL;
		buffer_code |= code;
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
		wake_up_interruptible(&read_wqueue);
	}
out:
	return IRQ_NONE;
}

static int  driver_register_irq(void *dev_id)
{
	if (irq == 0) {
		irq = 12;
	}

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
	/* nonseekable_open(inode, file); */
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
	printk(KERN_INFO LOG "Release of " MODULE_NAME " file by pid: %d\n", current->tgid);
	mutex_unlock(&open_mutex);
	return 0;
}


/* static ssize_t	driver_read(struct file *file, char __user *to, size_t size, loff_t *off) */
/* { */
/* 	ssize_t	    ret; */
/* 	uint64_t    len; */

/* 	while (atomic_read(&pending_data) == 0) { */
/* 		if (0 != wait_event_interruptible(read_wqueue, atomic_read(&pending_data) == 1)) */
/* 			return -ERESTARTSYS; */
/* 	} */
/* 	atomic_set(&pending_data, 0); */
/* 	if (*off >= sizeof(MODULE_NAME)) */
/* 		return (0); */
/* 	len = min(size, sizeof(MODULE_NAME)); */
/* 	ret = (ssize_t)copy_to_user(to, MODULE_NAME, len); */
/* 	if (ret != 0) { */
/* 		ret = -EPERM; */
/* 		goto out; */
/* 	} */
/* 	ret = (ssize_t)len; */
/* 	*off += len; */
/* out: */
/* 	return ret; */
/* } */

static int __init   init(void)
{
	int		    ret;

	ret = 0;

	if (irq != 0) {
		printk(KERN_INFO LOG "User requested default irq to be %d\n", irq);
	} else {
		irq = 1; //add defines
	}

	if (minor != 0) {
		printk(KERN_INFO LOG "User request minor number for device to be %u\n", minor);
	} else {
		minor = 42; //add defines
	}
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
