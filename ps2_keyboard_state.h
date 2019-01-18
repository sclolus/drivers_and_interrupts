// SPDX-License-Identifier: GPL-2.0
#ifndef __PS2_KEYBOARD_STATE_H__
# define __PS2_KEYBOARD_STATE_H__

# include "scan_code_sets.h"


# define PS2_CAPSLOCK_ACTIVE (1U << 15U)
# define PS2_LEFT_SHIFT_ACTIVE (1U << 14U)
# define PS2_RIGHT_SHIFT_ACTIVE (1U << 13U)
# define PS2_LEFT_ALT_ACTIVE (1U << 12U)
# define PS2_RIGHT_ALT_ACTIVE (1U << 11U)
# define PS2_ESCAPE_ACTIVE (1U << 10U)
# define PS2_LEFT_CTRL_ACTIVE (1U << 9U)
# define PS2_RIGHT_CTRL_ACTIVE (1U << 8U)
# define PS2_NUM_LOCK_ACTIVE (1U << 7U)
# define PS2_SCROLL_LOCK_ACTIVE (1U << 6U)

/*
  The whole point of this struct is to make us able to track the states of the keyboard in a elegant way.
  Also making us able to emulate the keyboard behavior from a list of `key_entry`.
 */
struct ps2_keyboard_state   {
	// states such as capslock on, shifts on, and so on...
	uint16_t		flags;

	// For compound codes, as the output buffer of the keyboard is one byte long, we did to collect parts of the key codes.
	uint64_t	        pending_code;

	// Describe if continuation bytes of the `pending_code` are expected from the device.
	bool			code_pending;

	// Current number of bytes composing the compound `pending_code` - 1
	uint8_t			current_code_index;

	// Current scan_code_set used by the keyboard
	struct scan_key_code	*scan_code_set;

	// Its number of elements
	uint64_t		set_len;
};

void			ps2_reset_pending_code(struct ps2_keyboard_state *state);
bool			ps2_add_to_pending_code(struct ps2_keyboard_state *state, uint8_t code);
bool			ps2_code_is_pending(struct ps2_keyboard_state *state);
bool		    	ps2_maybe_in_scan_set(struct ps2_keyboard_state *state, uint8_t code);
struct scan_key_code	*ps2_find_scan_key_code(struct ps2_keyboard_state *state);
bool			ps2_catch_modifiers(struct ps2_keyboard_state *state, struct scan_key_code *key);
char			ps2_key_name_with_modifiers(struct ps2_keyboard_state *state, struct scan_key_code *key_id);

/*
   Modifier callbacks
 */

typedef bool	(*ps2_modifier_callback_t)(struct ps2_keyboard_state *state, struct scan_key_code *key);




#endif /* __PS2_KEYBOARD_STATE_H__ */
