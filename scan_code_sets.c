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

bool		maybe_in_scan_set(struct scan_key_code *set, uint64_t set_len, uint64_t code, uint8_t current_index)
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

bool		key_code_has_ascii_value(struct scan_key_code *key_code)
{
	return key_code->ascii_value != 0x0;
}
