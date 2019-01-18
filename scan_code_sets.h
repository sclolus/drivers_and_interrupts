// SPDX-License-Identifier: GPL-2.0
#ifndef __SCAN_CODE_SETS_H__
# define __SCAN_CODE_SETS_H__

# include <linux/kernel.h>
# include <linux/fs.h>

#define PURE __attribute__((pure))

enum	ps2_key_state {
	PRESSED,
	RELEASED
};

struct scan_key_code {
	uint64_t		code;
	char		        *key_name;

	// State of the key press
	enum ps2_key_state	state;

	// ascii value, if any, else (char)0x0
	char			ascii_value;
};

struct	key_entry {
	// index inside the scan code set
	struct scan_key_code	*key_id;

	// Data at which the entry was performed
	struct timeval		date;

	struct list_head	head;
};

/*
  First scan code set of the PS/2 keyboards
 */

extern struct scan_key_code	scan_code_set_1[];

/*
  Second scan code set of the PS/2 keyboards
 */

extern struct scan_key_code	    scan_code_set_2[];

char			*ps2_key_state_to_string(enum ps2_key_state state);
struct scan_key_code	*find_scan_key_code(struct scan_key_code *set,
					uint64_t set_len,
					uint64_t code);
bool			maybe_in_scan_set(struct scan_key_code *set,
				uint64_t set_len,
				uint64_t code,
				uint8_t current_index);
bool			key_code_has_ascii_value(struct scan_key_code *key_code);
#endif /* __SCAN_CODE_SETS_H__ */
