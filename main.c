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
#include "ps2_keyboard_state.h"

MODULE_AUTHOR("sclolus");
MODULE_ALIAS("keyboard_driver");
MODULE_LICENSE("GPL v2");

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

struct scan_key_code	scan_code_set_1[] = {
	{ 0x1, "escape", PRESSED, 0x0 },
	{ 0x2, "1", PRESSED, '1' },
	{ 0x3, "2", PRESSED, '2' },
	{ 0x4, "3", PRESSED, '3' },
	{ 0x5, "4", PRESSED, '4' },
	{ 0x6, "5", PRESSED, '5' },
	{ 0x7, "6", PRESSED, '6' },
	{ 0x8, "7", PRESSED, '7' },
	{ 0x9, "8", PRESSED, '8' },
	{ 0xa, "9", PRESSED, '9' },
	{ 0xb, "0 (zero)", PRESSED, '0' },
	{ 0xc, "-", PRESSED, '-' },
	{ 0xd, "=", PRESSED, '=' },
	{ 0xe, "backspace", PRESSED, 0x0 },
	{ 0xf, "tab", PRESSED, '\t' },
	{ 0x10, "Q", PRESSED, 'q' },
	{ 0x11, "W", PRESSED, 'w' },
	{ 0x12, "E", PRESSED, 'e' },
	{ 0x13, "R", PRESSED, 'r' },
	{ 0x14, "T", PRESSED, 't' },
	{ 0x15, "Y", PRESSED, 'y' },
	{ 0x16, "U", PRESSED, 'u' },
	{ 0x17, "I", PRESSED, 'i' },
	{ 0x18, "O", PRESSED, 'o' },
	{ 0x19, "P", PRESSED, 'p' },
	{ 0x1a, "[", PRESSED, '[' },
	{ 0x1b, "]", PRESSED, ']' },
	{ 0x1c, "enter", PRESSED, '\n' }, // I mean yeah
	{ 0x1d, "left control", PRESSED, 0x0 },
	{ 0x1e, "A", PRESSED, 'a' },
	{ 0x1f, "S", PRESSED, 's' },
	{ 0x20, "D", PRESSED, 'd' },
	{ 0x21, "F", PRESSED, 'f' },
	{ 0x22, "G", PRESSED, 'g' },
	{ 0x23, "H", PRESSED, 'h' },
	{ 0x24, "J", PRESSED, 'j' },
	{ 0x25, "K", PRESSED, 'k' },
	{ 0x26, "L", PRESSED, 'l' },
	{ 0x27, ";", PRESSED, ';' },
	{ 0x28, "' (single quote)", PRESSED, '\'' },
	{ 0x29, "` (back tick)", PRESSED, '`' },
	{ 0x2a, "left shift", PRESSED, 0x0 },
	{ 0x2b, "\\", PRESSED, '\\' },
	{ 0x2c, "Z", PRESSED, 'z' },
	{ 0x2d, "X", PRESSED, 'x' },
	{ 0x2e, "C", PRESSED, 'c' },
	{ 0x2f, "V", PRESSED, 'v' },
	{ 0x30, "B", PRESSED, 'b' },
	{ 0x31, "N", PRESSED, 'n' },
	{ 0x32, "M", PRESSED, 'm' },
	{ 0x33, ",", PRESSED, ',' },
	{ 0x34, ".", PRESSED, '.' },
	{ 0x35, "/", PRESSED, '/' },
	{ 0x36, "right shift", PRESSED, 0x0 },
	{ 0x37, "(keypad) *", PRESSED, '*' },
	{ 0x38, "left alt", PRESSED, 0x0 },
	{ 0x39, "space", PRESSED, ' ' },
	{ 0x3a, "CapsLock", PRESSED, 0x0 },
	{ 0x3b, "F1", PRESSED, 0x0 },
	{ 0x3c, "F2", PRESSED, 0x0 },
	{ 0x3d, "F3", PRESSED, 0x0 },
	{ 0x3e, "F4", PRESSED, 0x0 },
	{ 0x3f, "F5", PRESSED, 0x0 },
	{ 0x40, "F6", PRESSED, 0x0 },
	{ 0x41, "F7", PRESSED, 0x0 },
	{ 0x42, "F8", PRESSED, 0x0 },
	{ 0x43, "F9", PRESSED, 0x0 },
	{ 0x44, "F10", PRESSED, 0x0 },
	{ 0x45, "NumberLock", PRESSED, 0x0 },
	{ 0x46, "ScrollLock", PRESSED, 0x0 },
	{ 0x47, "(keypad) 7", PRESSED, '7' },
	{ 0x48, "(keypad) 8", PRESSED, '8' },
	{ 0x49, "(keypad) 9", PRESSED, '9' },
	{ 0x4a, "(keypad) -", PRESSED, '-' },
	{ 0x4b, "(keypad) 4", PRESSED, '4' },
	{ 0x4c, "(keypad) 5", PRESSED, '5' },
	{ 0x4d, "(keypad) 6", PRESSED, '6' },
	{ 0x4e, "(keypad) +", PRESSED, '+' },
	{ 0x4f, "(keypad) 1", PRESSED, '1' },
	{ 0x50, "(keypad) 2", PRESSED, '2' },
	{ 0x51, "(keypad) 3", PRESSED, '3' },
	{ 0x52, "(keypad) 0", PRESSED, '0' },
	{ 0x53, "(keypad) .", PRESSED, '.' },
	{ 0x57, "F11", PRESSED, 0x0 },
	{ 0x58, "F12", PRESSED, 0x0 },
	{ 0x81, "escape", RELEASED, 0x0 },
	{ 0x82, "1", RELEASED, '1' },
	{ 0x83, "2", RELEASED, '2' },
	{ 0x84, "3", RELEASED, '3' },
	{ 0x85, "4", RELEASED, '4' },
	{ 0x86, "5", RELEASED, '5' },
	{ 0x87, "6", RELEASED, '6' },
	{ 0x88, "7", RELEASED, '6' },
	{ 0x89, "8", RELEASED, '8' },
	{ 0x8a, "9", RELEASED, '9' },
	{ 0x8b, "0 (zero)", RELEASED, '0' },
	{ 0x8c, "-", RELEASED, '-' },
	{ 0x8d, "=", RELEASED, '=' },
	{ 0x8e, "backspace", RELEASED, 0x0 },
	{ 0x8f, "tab", RELEASED, '\t' },
	{ 0x90, "Q", RELEASED, 'q' },
	{ 0x91, "W", RELEASED, 'w' },
	{ 0x92, "E", RELEASED, 'e' },
	{ 0x93, "R", RELEASED, 'r' },
	{ 0x94, "T", RELEASED, 't' },
	{ 0x95, "Y", RELEASED, 'y' },
	{ 0x96, "U", RELEASED, 'u' },
	{ 0x97, "I", RELEASED, 'i' },
	{ 0x98, "O", RELEASED, 'o' },
	{ 0x99, "P", RELEASED, 'p' },
	{ 0x9a, "[", RELEASED, '[' },
	{ 0x9b, "]", RELEASED, ']' },
	{ 0x9c, "enter", RELEASED, '\n' },
	{ 0x9d, "left control", RELEASED, 0x0 },
	{ 0x9e, "A", RELEASED, 'a' },
	{ 0x9f, "S", RELEASED, 's' },
	{ 0xa0, "D", RELEASED, 'd' },
	{ 0xa1, "F", RELEASED, 'f' },
	{ 0xa2, "G", RELEASED, 'g' },
	{ 0xa3, "H", RELEASED, 'h' },
	{ 0xa4, "J", RELEASED, 'j' },
	{ 0xa5, "K", RELEASED, 'k' },
	{ 0xa6, "L", RELEASED, 'l' },
	{ 0xa7, ";", RELEASED, ';' },
	{ 0xa8, "' (single quote)", RELEASED, '\'' },
	{ 0xa9, "` (back tick)", RELEASED, '`' },
	{ 0xaa, "left shift", RELEASED, 0x0 },
	{ 0xab, "\\", RELEASED, '\\' },
	{ 0xac, "Z", RELEASED, 'z' },
	{ 0xad, "X", RELEASED, 'x' },
	{ 0xae, "C", RELEASED, 'c' },
	{ 0xaf, "V", RELEASED, 'v' },
	{ 0xb0, "B", RELEASED, 'b' },
	{ 0xb1, "N", RELEASED, 'n' },
	{ 0xb2, "M", RELEASED, 'm' },
	{ 0xb3, ",", RELEASED, ',' },
	{ 0xb4, ".", RELEASED, '.' },
	{ 0xb5, "/", RELEASED, '/' },
	{ 0xb6, "right shift", RELEASED, 0x0 },
	{ 0xb7, "(keypad) *", RELEASED, '*' },
	{ 0xb8, "left alt", RELEASED, 0x0 },
	{ 0xb9, "space", RELEASED, ' ' },
	{ 0xba, "CapsLock", RELEASED, 0x0 },
	{ 0xbb, "F1", RELEASED, 0x0 },
	{ 0xbc, "F2", RELEASED, 0x0 },
	{ 0xbd, "F3", RELEASED, 0x0 },
	{ 0xbe, "F4", RELEASED, 0x0 },
	{ 0xbf, "F5", RELEASED, 0x0 },
	{ 0xc0, "F6", RELEASED, 0x0 },
	{ 0xc1, "F7", RELEASED, 0x0 },
	{ 0xc2, "F8", RELEASED, 0x0 },
	{ 0xc3, "F9", RELEASED, 0x0 },
	{ 0xc4, "F10", RELEASED, 0x0 },
	{ 0xc5, "NumberLock", RELEASED, 0x0 },
	{ 0xc6, "ScrollLock", RELEASED, 0x0 },
	{ 0xc7, "(keypad) 7", RELEASED, '7' },
	{ 0xc8, "(keypad) 8", RELEASED, '8' },
	{ 0xc9, "(keypad) 9", RELEASED, '9' },
	{ 0xca, "(keypad) -", RELEASED, '-' },
	{ 0xcb, "(keypad) 4", RELEASED, '4' },
	{ 0xcc, "(keypad) 5", RELEASED, '5' },
	{ 0xcd, "(keypad) 6", RELEASED, '6' },
	{ 0xce, "(keypad) +", RELEASED, '+' },
	{ 0xcf, "(keypad) 1", RELEASED, '1' },
	{ 0xd0, "(keypad) 2", RELEASED, '2' },
	{ 0xd1, "(keypad) 3", RELEASED, '4' },
	{ 0xd2, "(keypad) 0", RELEASED, '0' },
	{ 0xd3, "(keypad) .", RELEASED, '.' },
	{ 0xd7, "F11", RELEASED, 0x0 },
	{ 0xd8, "F12", RELEASED, 0x0 },
	{ 0xe010, "(multimedia) previous track", PRESSED, 0x0 },
	{ 0xe019, "(multimedia) next track", PRESSED, 0x0 },
	{ 0xe01c, "(keypad) enter", PRESSED, '\n' },
	{ 0xe01d, "right control", PRESSED, 0x0 },
	{ 0xe020, "(multimedia) mute", PRESSED, 0x0 },
	{ 0xe021, "(multimedia) calculator", PRESSED, 0x0 },
	{ 0xe022, "(multimedia) play", PRESSED, 0x0 },
	{ 0xe024, "(multimedia) stop", PRESSED, 0x0 },
	{ 0xe02e, "(multimedia) volume down", PRESSED, 0x0 },
	{ 0xe030, "(multimedia) volume up", PRESSED, 0x0 },
	{ 0xe032, "(multimedia) WWW home", PRESSED, 0x0 },
	{ 0xe035, "(keypad) /", PRESSED, '/' },
	{ 0xe038, "right alt (or altGr)", PRESSED, 0x0 },
	{ 0xe047, "home", PRESSED, 0x0 },
	{ 0xe048, "cursor up", PRESSED, 0x0 },
	{ 0xe049, "page up", PRESSED, 0x0 },
	{ 0xe04b, "cursor left", PRESSED, 0x0 },
	{ 0xe04d, "cursor right", PRESSED, 0x0 },
	{ 0xe04f, "end", PRESSED, 0x0 },
	{ 0xe050, "cursor down", PRESSED, 0x0 },
	{ 0xe051, "page down", PRESSED, 0x0 },
	{ 0xe052, "insert", PRESSED, 0x0 },
	{ 0xe053, "delete", PRESSED, 0x0 },
	{ 0xe05b, "left GUI", PRESSED, 0x0 },
	{ 0xe05c, "right GUI", PRESSED, 0x0 },
	{ 0xe05d, "\"apps\"", PRESSED, 0x0 },
	{ 0xe05e, "(ACPI) power", PRESSED, 0x0 },
	{ 0xe05f, "(ACPI) sleep", PRESSED, 0x0 },
	{ 0xe063, "(ACPI) wake", PRESSED, 0x0 },
	{ 0xe065, "(multimedia) WWW search", PRESSED, 0x0 },
	{ 0xe066, "(multimedia) WWW favorites", PRESSED, 0x0 },
	{ 0xe067, "(multimedia) WWW refresh", PRESSED, 0x0 },
	{ 0xe068, "(multimedia) WWW stop", PRESSED, 0x0 },
	{ 0xe069, "(multimedia) WWW forward", PRESSED, 0x0 },
	{ 0xe06a, "(multimedia) WWW back", PRESSED, 0x0 },
	{ 0xe06b, "(multimedia) my computer", PRESSED, 0x0 },
	{ 0xe06c, "(multimedia) email", PRESSED, 0x0 },
	{ 0xe06d, "(multimedia) media select", PRESSED, 0x0 },
	{ 0xe090, "(multimedia) previous track", RELEASED, 0x0 },
	{ 0xe099, "(multimedia) next track", RELEASED, 0x0 },
	{ 0xe09c, "(keypad) enter", RELEASED, '\n' },
	{ 0xe09d, "right control", RELEASED, 0x0 },
	{ 0xe0a0, "(multimedia) mute", RELEASED, 0x0 },
	{ 0xe0a1, "(multimedia) calculator", RELEASED, 0x0 },
	{ 0xe0a2, "(multimedia) play", RELEASED, 0x0 },
	{ 0xe0a4, "(multimedia) stop", RELEASED, 0x0 },
	{ 0xe0ae, "(multimedia) volume down", RELEASED, 0x0 },
	{ 0xe0b0, "(multimedia) volume up", RELEASED, 0x0 },
	{ 0xe0b2, "(multimedia) WWW home", RELEASED, 0x0 },
	{ 0xe0b5, "(keypad) /", RELEASED, '/' },
	{ 0xe0b8, "right alt (or altGr)", RELEASED, 0x0 },
	{ 0xe0c7, "home", RELEASED, 0x0 },
	{ 0xe0c8, "cursor up", RELEASED, 0x0 },
	{ 0xe0c9, "page up", RELEASED, 0x0 },
	{ 0xe0cb, "cursor left", RELEASED, 0x0 },
	{ 0xe0cd, "cursor right", RELEASED, 0x0 },
	{ 0xe0cf, "end", RELEASED, 0x0 },
	{ 0xe0d0, "cursor down", RELEASED, 0x0 },
	{ 0xe0d1, "page down", RELEASED, 0x0 },
	{ 0xe0d2, "insert", RELEASED, 0x0 },
	{ 0xe0d3, "delete", RELEASED, 0x0 },
	{ 0xe0db, "left GUI", RELEASED, 0x0 },
	{ 0xe0dc, "right GUI", RELEASED, 0x0 },
	{ 0xe0dd, "\"apps\"", RELEASED, 0x0 },
	{ 0xe0de, "(ACPI) power", RELEASED, 0x0 },
	{ 0xe0df, "(ACPI) sleep", RELEASED, 0x0 },
	{ 0xe0e3, "(ACPI) wake", RELEASED, 0x0 },
	{ 0xe0e5, "(multimedia) WWW search", RELEASED, 0x0 },
	{ 0xe0e6, "(multimedia) WWW favorites", RELEASED, 0x0 },
	{ 0xe0e7, "(multimedia) WWW refresh", RELEASED, 0x0 },
	{ 0xe0e8, "(multimedia) WWW stop", RELEASED, 0x0 },
	{ 0xe0e9, "(multimedia) WWW forward", RELEASED, 0x0 },
	{ 0xe0ea, "(multimedia) WWW back", RELEASED, 0x0 },
	{ 0xe0eb, "(multimedia) my computer", RELEASED, 0x0 },
	{ 0xe0ec, "(multimedia) email", RELEASED, 0x0 },
	{ 0xe0ed, "(multimedia) media select", RELEASED, 0x0 },
	{ 0xe02ae037, "print screen", PRESSED, 0x0 },
	{ 0xe0b7e0aa, "print screen", RELEASED, 0x0 },
	{ 0xe11d45e19dc5, "pause", PRESSED, 0x0 },
};

struct scan_key_code scan_code_set_2[] = {
	{ 0x1, "escape", PRESSED, 0x0 },
	{ 0x2, "1", PRESSED, '1' },
	{ 0x3, "2", PRESSED, '2' },
	{ 0x4, "3", PRESSED, '3' },
	{ 0x5, "4", PRESSED, '4' },
	{ 0x6, "5", PRESSED, '5' },
	{ 0x7, "6", PRESSED, '6' },
	{ 0x8, "7", PRESSED, '7' },
	{ 0x9, "8", PRESSED, '8' },
	{ 0xa, "9", PRESSED, '9' },
	{ 0xb, "0 (zero)", PRESSED, '0' },
	{ 0xc, "-", PRESSED, '-' },
	{ 0xd, "=", PRESSED, '=' },
	{ 0xe, "backspace", PRESSED, 0x0 },
	{ 0xf, "tab", PRESSED, '\t' },
	{ 0x10, "Q", PRESSED, 'q' },
	{ 0x11, "W", PRESSED, 'w' },
	{ 0x12, "E", PRESSED, 'e' },
	{ 0x13, "R", PRESSED, 'r' },
	{ 0x14, "T", PRESSED, 't' },
	{ 0x15, "Y", PRESSED, 'y' },
	{ 0x16, "U", PRESSED, 'u' },
	{ 0x17, "I", PRESSED, 'i' },
	{ 0x18, "O", PRESSED, 'o' },
	{ 0x19, "P", PRESSED, 'p' },
	{ 0x1a, "[", PRESSED, '[' },
	{ 0x1b, "]", PRESSED, ']' },
	{ 0x1c, "enter", PRESSED, '\n' },
	{ 0x1d, "left control", PRESSED, 0x0 },
	{ 0x1e, "A", PRESSED, 'a' },
	{ 0x1f, "S", PRESSED, 's' },
	{ 0x20, "D", PRESSED, 'd' },
	{ 0x21, "F", PRESSED, 'f' },
	{ 0x22, "G", PRESSED, 'g' },
	{ 0x23, "H", PRESSED, 'h' },
	{ 0x24, "J", PRESSED, 'j' },
	{ 0x25, "K", PRESSED, 'k' },
	{ 0x26, "L", PRESSED, 'l' },
	{ 0x27, ";", PRESSED, ';' },
	{ 0x28, "' (single quote)", PRESSED, '\'' },
	{ 0x29, "` (back tick)", PRESSED, '`' },
	{ 0x2a, "left shift", PRESSED, 0x0 },
	{ 0x2b, "\\", PRESSED, '\\' },
	{ 0x2c, "Z", PRESSED, 'z' },
	{ 0x2d, "X", PRESSED, 'x' },
	{ 0x2e, "C", PRESSED, 'c' },
	{ 0x2f, "V", PRESSED, 'v' },
	{ 0x30, "B", PRESSED, 'b' },
	{ 0x31, "N", PRESSED, 'n' },
	{ 0x32, "M", PRESSED, 'm' },
	{ 0x33, ",", PRESSED, ',' },
	{ 0x34, ".", PRESSED, '.' },
	{ 0x35, "/", PRESSED, '/' },
	{ 0x36, "right shift", PRESSED, 0x0 },
	{ 0x37, "(keypad) *", PRESSED, '*' },
	{ 0x38, "left alt", PRESSED, 0x0 },
	{ 0x39, "space", PRESSED, ' ' },
	{ 0x3a, "CapsLock", PRESSED, 0x0 },
	{ 0x3b, "F1", PRESSED, 0x0 },
	{ 0x3c, "F2", PRESSED, 0x0 },
	{ 0x3d, "F3", PRESSED, 0x0 },
	{ 0x3e, "F4", PRESSED, 0x0 },
	{ 0x3f, "F5", PRESSED, 0x0 },
	{ 0x40, "F6", PRESSED, 0x0 },
	{ 0x41, "F7", PRESSED, 0x0 },
	{ 0x42, "F8", PRESSED, 0x0 },
	{ 0x43, "F9", PRESSED, 0x0 },
	{ 0x44, "F10", PRESSED, 0x0 },
	{ 0x45, "NumberLock", PRESSED, 0x0 },
	{ 0x46, "ScrollLock", PRESSED, 0x0 },
	{ 0x47, "(keypad) 7", PRESSED, '7' },
	{ 0x48, "(keypad) 8", PRESSED, '8' },
	{ 0x49, "(keypad) 9", PRESSED, '9' },
	{ 0x4a, "(keypad) -", PRESSED, '-' },
	{ 0x4b, "(keypad) 4", PRESSED, '4' },
	{ 0x4c, "(keypad) 5", PRESSED, '5' },
	{ 0x4d, "(keypad) 6", PRESSED, '6' },
	{ 0x4e, "(keypad) +", PRESSED, '+' },
	{ 0x4f, "(keypad) 1", PRESSED, '1' },
	{ 0x50, "(keypad) 2", PRESSED, '2' },
	{ 0x51, "(keypad) 3", PRESSED, '3' },
	{ 0x52, "(keypad) 0", PRESSED, '0' },
	{ 0x53, "(keypad) .", PRESSED, '.' },
	{ 0x57, "F11", PRESSED, 0x0 },
	{ 0x58, "F12", PRESSED, 0x0 },
	{ 0x81, "escape", RELEASED, 0x0 },
	{ 0x82, "1", RELEASED, '1' },
	{ 0x83, "2", RELEASED, '2' },
	{ 0x84, "3", RELEASED, '3' },
	{ 0x85, "4", RELEASED, '4' },
	{ 0x86, "5", RELEASED, '5' },
	{ 0x87, "6", RELEASED, '6' },
	{ 0x88, "7", RELEASED, '7' },
	{ 0x89, "8", RELEASED, '8' },
	{ 0x8a, "9", RELEASED, '9' },
	{ 0x8b, "0 (zero)", RELEASED, '0' },
	{ 0x8c, "-", RELEASED, '-' },
	{ 0x8d, "=", RELEASED, '=' },
	{ 0x8e, "backspace", RELEASED, 0x0 },
	{ 0x8f, "tab", RELEASED, '\t' },
	{ 0x90, "Q", RELEASED, 'q' },
	{ 0x91, "W", RELEASED, 'w' },
	{ 0x92, "E", RELEASED, 'e' },
	{ 0x93, "R", RELEASED, 'r' },
	{ 0x94, "T", RELEASED, 't' },
	{ 0x95, "Y", RELEASED, 'y' },
	{ 0x96, "U", RELEASED, 'u' },
	{ 0x97, "I", RELEASED, 'i' },
	{ 0x98, "O", RELEASED, 'o' },
	{ 0x99, "P", RELEASED, 'p' },
	{ 0x9a, "[", RELEASED, '[' },
	{ 0x9b, "]", RELEASED, ']' },
	{ 0x9c, "enter", RELEASED, '\n' },
	{ 0x9d, "left control", RELEASED, 0x0 },
	{ 0x9e, "A", RELEASED, 'a' },
	{ 0x9f, "S", RELEASED, 's' },
	{ 0xa0, "D", RELEASED, 'd' },
	{ 0xa1, "F", RELEASED, 'f' },
	{ 0xa2, "G", RELEASED, 'g' },
	{ 0xa3, "H", RELEASED, 'h' },
	{ 0xa4, "J", RELEASED, 'j' },
	{ 0xa5, "K", RELEASED, 'k' },
	{ 0xa6, "L", RELEASED, 'l' },
	{ 0xa7, ";", RELEASED, ';' },
	{ 0xa8, "' (single quote)", RELEASED, '\'' },
	{ 0xa9, "` (back tick)", RELEASED, '`' },
	{ 0xaa, "left shift", RELEASED, 0x0 },
	{ 0xab, "\\", RELEASED, '\\' },
	{ 0xac, "Z", RELEASED, 'z' },
	{ 0xad, "X", RELEASED, 'x' },
	{ 0xae, "C", RELEASED, 'c' },
	{ 0xaf, "V", RELEASED, 'v' },
	{ 0xb0, "B", RELEASED, 'b' },
	{ 0xb1, "N", RELEASED, 'n' },
	{ 0xb2, "M", RELEASED, 'm' },
	{ 0xb3, ",", RELEASED, ',' },
	{ 0xb4, ".", RELEASED, '.' },
	{ 0xb5, "/", RELEASED, '/' },
	{ 0xb6, "right shift", RELEASED, 0x0 },
	{ 0xb7, "(keypad) *", RELEASED, '*' },
	{ 0xb8, "left alt", RELEASED, 0x0 },
	{ 0xb9, "space", RELEASED, ' ' },
	{ 0xba, "CapsLock", RELEASED, 0x0 },
	{ 0xbb, "F1", RELEASED, 0x0 },
	{ 0xbc, "F2", RELEASED, 0x0 },
	{ 0xbd, "F3", RELEASED, 0x0 },
	{ 0xbe, "F4", RELEASED, 0x0 },
	{ 0xbf, "F5", RELEASED, 0x0 },
	{ 0xc0, "F6", RELEASED, 0x0 },
	{ 0xc1, "F7", RELEASED, 0x0 },
	{ 0xc2, "F8", RELEASED, 0x0 },
	{ 0xc3, "F9", RELEASED, 0x0 },
	{ 0xc4, "F10", RELEASED, 0x0 },
	{ 0xc5, "NumberLock", RELEASED, 0x0 },
	{ 0xc6, "ScrollLock", RELEASED, 0x0 },
	{ 0xc7, "(keypad) 7", RELEASED, '7' },
	{ 0xc8, "(keypad) 8", RELEASED, '8' },
	{ 0xc9, "(keypad) 9", RELEASED, '9' },
	{ 0xca, "(keypad) -", RELEASED, '-' },
	{ 0xcb, "(keypad) 4", RELEASED, '4' },
	{ 0xcc, "(keypad) 5", RELEASED, '5' },
	{ 0xcd, "(keypad) 6", RELEASED, '6' },
	{ 0xce, "(keypad) +", RELEASED, '+' },
	{ 0xcf, "(keypad) 1", RELEASED, '1' },
	{ 0xd0, "(keypad) 2", RELEASED, '2' },
	{ 0xd1, "(keypad) 3", RELEASED, '3' },
	{ 0xd2, "(keypad) 0", RELEASED, '0' },
	{ 0xd3, "(keypad) .", RELEASED, '.' },
	{ 0xd7, "F11", RELEASED, 0x0 },
	{ 0xd8, "F12", RELEASED, 0x0 },
	{ 0xe010, "(multimedia) previous track", PRESSED, 0x0 },
	{ 0xe019, "(multimedia) next track", PRESSED, 0x0 },
	{ 0xe01c, "(keypad) enter", PRESSED, '\n' },
	{ 0xe01d, "right control", PRESSED, 0x0 },
	{ 0xe020, "(multimedia) mute", PRESSED, 0x0 },
	{ 0xe021, "(multimedia) calculator", PRESSED, 0x0 },
	{ 0xe022, "(multimedia) play", PRESSED, 0x0 },
	{ 0xe024, "(multimedia) stop", PRESSED, 0x0 },
	{ 0xe02e, "(multimedia) volume down", PRESSED, 0x0 },
	{ 0xe030, "(multimedia) volume up", PRESSED, 0x0 },
	{ 0xe032, "(multimedia) WWW home", PRESSED, 0x0 },
	{ 0xe035, "(keypad) /", PRESSED, '/' },
	{ 0xe038, "right alt (or altGr)", PRESSED, 0x0 },
	{ 0xe047, "home", PRESSED, 0x0 },
	{ 0xe048, "cursor up", PRESSED, 0x0 },
	{ 0xe049, "page up", PRESSED, 0x0 },
	{ 0xe04b, "cursor left", PRESSED, 0x0 },
	{ 0xe04d, "cursor right", PRESSED, 0x0 },
	{ 0xe04f, "end", PRESSED, 0x0 },
	{ 0xe050, "cursor down", PRESSED, 0x0 },
	{ 0xe051, "page down", PRESSED, 0x0 },
	{ 0xe052, "insert", PRESSED, 0x0 },
	{ 0xe053, "delete", PRESSED, 0x0 },
	{ 0xe05b, "left GUI", PRESSED, 0x0 },
	{ 0xe05c, "right GUI", PRESSED, 0x0 },
	{ 0xe05d, "\"apps\"", PRESSED, 0x0 },
	{ 0xe05e, "(ACPI) power", PRESSED, 0x0 },
	{ 0xe05f, "(ACPI) sleep", PRESSED, 0x0 },
	{ 0xe063, "(ACPI) wake", PRESSED, 0x0 },
	{ 0xe065, "(multimedia) WWW search", PRESSED, 0x0 },
	{ 0xe066, "(multimedia) WWW favorites", PRESSED, 0x0 },
	{ 0xe067, "(multimedia) WWW refresh", PRESSED, 0x0 },
	{ 0xe068, "(multimedia) WWW stop", PRESSED, 0x0 },
	{ 0xe069, "(multimedia) WWW forward", PRESSED, 0x0 },
	{ 0xe06a, "(multimedia) WWW back", PRESSED, 0x0 },
	{ 0xe06b, "(multimedia) my computer", PRESSED, 0x0 },
	{ 0xe06c, "(multimedia) email", PRESSED, 0x0 },
	{ 0xe06d, "(multimedia) media select", PRESSED, 0x0 },
	{ 0xe090, "(multimedia) previous track", RELEASED, 0x0 },
	{ 0xe099, "(multimedia) next track", RELEASED, 0x0 },
	{ 0xe09c, "(keypad) enter", RELEASED, '\n' },
	{ 0xe09d, "right control", RELEASED, 0x0 },
	{ 0xe0a0, "(multimedia) mute", RELEASED, 0x0 },
	{ 0xe0a1, "(multimedia) calculator", RELEASED, 0x0 },
	{ 0xe0a2, "(multimedia) play", RELEASED, 0x0 },
	{ 0xe0a4, "(multimedia) stop", RELEASED, 0x0 },
	{ 0xe0ae, "(multimedia) volume down", RELEASED, 0x0 },
	{ 0xe0b0, "(multimedia) volume up", RELEASED, 0x0 },
	{ 0xe0b2, "(multimedia) WWW home", RELEASED, 0x0 },
	{ 0xe0b5, "(keypad) /", RELEASED, '/' },
	{ 0xe0b8, "right alt (or altGr)", RELEASED, 0x0 },
	{ 0xe0c7, "home", RELEASED, 0x0 },
	{ 0xe0c8, "cursor up", RELEASED, 0x0 },
	{ 0xe0c9, "page up", RELEASED, 0x0 },
	{ 0xe0cb, "cursor left", RELEASED, 0x0 },
	{ 0xe0cd, "cursor right", RELEASED, 0x0 },
	{ 0xe0cf, "end", RELEASED, 0x0 },
	{ 0xe0d0, "cursor down", RELEASED, 0x0 },
	{ 0xe0d1, "page down", RELEASED, 0x0 },
	{ 0xe0d2, "insert", RELEASED, 0x0 },
	{ 0xe0d3, "delete", RELEASED, 0x0 },
	{ 0xe0db, "left GUI", RELEASED, 0x0 },
	{ 0xe0dc, "right GUI", RELEASED, 0x0 },
	{ 0xe0dd, "\"apps\"", RELEASED, 0x0 },
	{ 0xe0de, "(ACPI) power", RELEASED, 0x0 },
	{ 0xe0df, "(ACPI) sleep", RELEASED, 0x0 },
	{ 0xe0e3, "(ACPI) wake", RELEASED, 0x0 },
	{ 0xe0e5, "(multimedia) WWW search", RELEASED, 0x0 },
	{ 0xe0e6, "(multimedia) WWW favorites", RELEASED, 0x0 },
	{ 0xe0e7, "(multimedia) WWW refresh", RELEASED, 0x0 },
	{ 0xe0e8, "(multimedia) WWW stop", RELEASED, 0x0 },
	{ 0xe0e9, "(multimedia) WWW forward", RELEASED, 0x0 },
	{ 0xe0ea, "(multimedia) WWW back", RELEASED, 0x0 },
	{ 0xe0eb, "(multimedia) my computer", RELEASED, 0x0 },
	{ 0xe0ec, "(multimedia) email", RELEASED, 0x0 },
	{ 0xe0ed, "(multimedia) media select", RELEASED, 0x0 },
	{ 0xe02ae037, "print screen", PRESSED, 0x0 },
	{ 0xe0b7e0aa, "print screen", RELEASED, 0x0 },
	{ 0xe11d45e19dc5, "pause", PRESSED, 0x0 },
};

static struct ps2_keyboard_state	keyboard_state = {
	.scan_code_set = scan_code_set_2,
	.set_len = sizeof(scan_code_set_2) / sizeof(*scan_code_set_2),
};

static irqreturn_t	keyboard_irq_handler(int irq, void *dev_id)
{
	uint8_t					code;
	struct scan_key_code			*key_id;

	mb();
	code = inb(KEYBOARD_IOPORT);

	if (!ps2_maybe_in_scan_set(&keyboard_state, code)) {
		ps2_add_to_pending_code(&keyboard_state, code);
		printk(KERN_WARNING LOG "Dropping code: %llx, as it may not belong to the scan set table in use\n", keyboard_state.pending_code);
		ps2_reset_pending_code(&keyboard_state);
		return IRQ_NONE;
	}

	WARN_ON(!ps2_add_to_pending_code(&keyboard_state, code));
	key_id = ps2_find_scan_key_code(&keyboard_state);

	if (key_id == NULL) {
		printk(KERN_INFO LOG "Current buffered code: %#02llx\n", keyboard_state.pending_code);
	} else {
		struct timeval	    now;
		long long	    hours;
		long long	    minutes;
		long long	    seconds;
		struct key_entry    *entry;
		char		    c;

		if (NULL == (entry = kmalloc(sizeof(struct key_entry), GFP_ATOMIC))) {
			// Not much to do if kmalloc fails. just pop up a warning
			printk(KERN_WARNING LOG "Failed to allocated for key_entry, entry log will be lost\n");
			ps2_reset_pending_code(&keyboard_state);
			goto out;
		}
		do_gettimeofday(&now);

		entry->date = now;
		entry->key_id = key_id;

		spin_lock(&key_list_spinlock);
		list_add_tail(&entry->head, &key_entry_list);
		spin_unlock(&key_list_spinlock);

		hours = (now.tv_sec / 3600) % 24;
		now.tv_sec %= 3600;
		minutes = now.tv_sec / 60;
		now.tv_sec %= 60;
		seconds = now.tv_sec;

		c = ps2_key_name_with_modifiers(&keyboard_state, key_id);
		if (c) {
			printk(KERN_INFO LOG "%02lld:%02lld:%02lld %c(%#02llx) %s\n",
				hours,
				minutes,
				seconds,
				c,
				key_id->code,
				ps2_key_state_to_string(key_id->state));
		} else {
			printk(KERN_INFO LOG "%02lld:%02lld:%02lld %s(%#02llx) %s\n",
				hours,
				minutes,
				seconds,
				key_id->key_name,
				key_id->code,
				ps2_key_state_to_string(key_id->state));
		}
		ps2_reset_pending_code(&keyboard_state);
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

		ret = wait_event_interruptible(read_wqueue, !list_is_last(list, &key_entry_list));
		if (ret)
			return -ERESTARTSYS;

	}

	((struct seq_file *)file->private_data)->private = &key_entry_list;
	return ret;
}

static void *driver_seq_start(struct seq_file *seq_file, loff_t *pos)
{
	struct list_head *list = seq_file->private;
	int		 ret;

	list = seq_list_start(&key_entry_list, *pos);

	if (list == NULL) {
		list =  key_entry_list.prev;
	}

	while (list_is_last(list, &key_entry_list)) {
		ret = wait_event_interruptible(read_wqueue, !list_is_last(list, &key_entry_list));
		if (ret)
			return (void *)-ERESTARTSYS;
	}


	//	unsigned long	flags;

//	spin_lock_irqsave(&key_list_spinlock, flags);

//	spin_lock_irqsave(&key_list_spinlock, flags);
	return list;
}

static void driver_seq_stop(struct seq_file *seq_file, void *v)
{

}

static void *driver_seq_next(struct seq_file *seq_file, void *v, loff_t *pos)
{
	if (list_is_last(v, &key_entry_list))
		return NULL;
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

	key_code = key_entry->key_id; //secure this
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
	struct list_head *cur; //actually this code should be in cleanup module...
	struct list_head *tmp;
	struct key_entry *key_entry;

	free_irq(irq, &key_entry_list);
	misc_deregister(&driver_data.device);
	list_for_each_safe(cur, tmp, &key_entry_list) {
		key_entry = list_entry(cur, struct key_entry, head);
		WARN_ON(key_entry == NULL); //why the fuck I am doing this

		if (key_code_has_ascii_value(key_entry->key_id)
			&& key_entry->key_id->state == PRESSED) {
			printk(KERN_INFO LOG "%c",
				ps2_key_name_with_modifiers(&keyboard_state, key_entry->key_id));
		}
		list_del(cur);
		kfree(key_entry);
	}
	printk(KERN_INFO LOG "Cleanup up module\n");
}
module_exit(cleanup);
