// SPDX-License-Identifier: GPL-2.0
#include "scan_code_sets.h"

#define LOG __FILE__": "

PURE char	*ps2_key_state_to_string(enum ps2_key_state state)
{
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

struct scan_key_code *find_scan_key_code(struct scan_key_code *set, uint64_t set_len, uint64_t code)
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

static uint8_t	get_nth_byte_in_key_code(struct scan_key_code *set, uint8_t n)
{
	uint64_t    i = 0;
	uint64_t    byte;

	while (i < sizeof(uint64_t)) {
//		printk(KERN_INFO LOG "%lld: %llx\n", i, (set->code & (0xFF00000000000000 >> (i * 8UL))));
		if ((set->code & (0xFF00000000000000 >> (i * 8UL))) != 0) {
			byte = (set->code & (0xFF00000000000000 >> (i * 8UL)));
			return byte >> (7U - (i + n)) * 8UL;
		}
		i++;
	}
//	printk(KERN_WARNING LOG "return this\n");
	return 0x0;
}

bool		maybe_in_scan_set(struct scan_key_code *set, uint64_t set_len, uint64_t code, uint8_t current_index)
{
	uint64_t    i;
	uint8_t	    byte;

	i = 0;
	WARN_ON(current_index > sizeof(uint64_t));
	while (i < set_len) {
		byte = get_nth_byte_in_key_code(&set[i], current_index);
		/* printk(KERN_INFO LOG "byte: %x at index: %d\n", byte, current_index); */
		/* printk(KERN_INFO LOG "%llx == %x\n", code, byte); */
		if (code == byte)
			return true;
		i++;
	}
	return false;
}

bool		key_code_has_ascii_value(struct scan_key_code *key_code)
{
	return key_code->ascii_value != 0x0;
}
