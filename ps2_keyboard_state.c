// SPDX-License-Identifier: GPL-2.0
#include "ps2_keyboard_state.h"
#include "scan_code_sets.h"
#include <linux/kernel.h>

#define LOG __FILE__": "

inline bool	ps2_code_is_pending(struct ps2_keyboard_state *state)
{
	return state->code_pending;
}

inline bool	ps2_add_to_pending_code(struct ps2_keyboard_state *state, uint8_t code)
{
	if (state->current_code_index == 8U) {
		printk(KERN_WARNING LOG "Failed to add byte to pending code as the compound code would exceed the maximum size\n");
		printk(KERN_WARNING LOG "Current pending code is dropped\n");
		ps2_reset_pending_code(state);
		return false;
	}
	if (state->code_pending == true) {
		state->pending_code <<= 8U;
		state->pending_code |= code;
		state->current_code_index++;
	} else {
		state->code_pending = true;
		state->pending_code = code;
		state->current_code_index = 1;
	}
	return true;
}

inline void	ps2_reset_pending_code(struct ps2_keyboard_state *state)
{
	state->pending_code = 0;
	state->code_pending = false;
	state->current_code_index = 0;
}

inline void	ps2_reset_flags(struct ps2_keyboard_state *state)
{
	state->flags = 0;
}

inline bool	ps2_maybe_in_scan_set(struct ps2_keyboard_state *state, uint8_t code)
{
	printk(KERN_INFO LOG "index: %d\n", state->current_code_index);
	return maybe_in_scan_set(state->scan_code_set, state->set_len, code, state->current_code_index);
}

static bool	escape_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_ESCAPE_ACTIVE;
	} else {
		state->flags &= ~PS2_ESCAPE_ACTIVE;
	}
	return true;
}

static bool	left_control_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_LEFT_CTRL_ACTIVE;
	} else {
		state->flags &= ~PS2_LEFT_CTRL_ACTIVE;
	}
	return true;
}

static bool	right_control_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_RIGHT_CTRL_ACTIVE;
	} else {
		state->flags &= ~PS2_RIGHT_CTRL_ACTIVE;
	}
	return true;

}

static bool	left_shift_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_LEFT_SHIFT_ACTIVE;
	} else {
		state->flags &= ~PS2_LEFT_SHIFT_ACTIVE;
	}
	return true;
}

static bool	right_shift_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_RIGHT_SHIFT_ACTIVE;
	} else {
		state->flags &= ~PS2_RIGHT_SHIFT_ACTIVE;
	}
	return true;
}

static bool	capslock_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags ^= PS2_CAPSLOCK_ACTIVE;
	}
	return true;
}

static bool	number_lock_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags ^= PS2_NUM_LOCK_ACTIVE;
	}
	return true;
}

static bool	scroll_lock_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags ^= PS2_SCROLL_LOCK_ACTIVE;
	}
	return true;
}

static bool	left_alt_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_LEFT_ALT_ACTIVE;
	} else {
		state->flags &= ~PS2_LEFT_ALT_ACTIVE;
	}
	return true;
}

static bool	right_alt_callback(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	if (key->state == PRESSED) {
		state->flags |= PS2_RIGHT_ALT_ACTIVE;
	} else {
		state->flags &= ~PS2_RIGHT_ALT_ACTIVE;
	}
	return true;
}

inline bool	ps2_catch_modifiers(struct ps2_keyboard_state *state, struct scan_key_code *key)
{
	static const char *const modifier_names[] = {
		"escape",
		"left control",
		"right control",
		"left shift",
		"right shift",
		"CapsLock",
		"NumberLock",
		"ScrollLock",
		"left alt",
		"right alt",
	};
	static const ps2_modifier_callback_t	callbacks[] = {
		&escape_callback,
		&left_control_callback,
		&right_control_callback,
		&left_shift_callback,
		&right_shift_callback,
		&capslock_callback,
		&number_lock_callback,
		&scroll_lock_callback,
		&left_alt_callback,
		&right_alt_callback,
	};
	uint32_t    i;

	i = 0;
	while (i < sizeof(callbacks) / sizeof(*callbacks)) {
		if (!strcmp(key->key_name, modifier_names[i])) {
			printk(KERN_INFO LOG "catch a keyboard modifier: %s\n", modifier_names[i]);
			return callbacks[i](state, key);
		}
		i++;
	}
	return false;
}

struct scan_key_code *ps2_find_scan_key_code(struct ps2_keyboard_state *state)
{
	struct scan_key_code *key;

	if (state->code_pending == false)
		return NULL;
	key = find_scan_key_code(state->scan_code_set, state->set_len, state->pending_code);

	if (key) {
		ps2_catch_modifiers(state, key);
	}
	return key;
}

static bool	    ps2_is_shifted(struct ps2_keyboard_state *state)
{
	return (state->flags & (PS2_CAPSLOCK_ACTIVE
			| PS2_LEFT_SHIFT_ACTIVE
					| PS2_RIGHT_SHIFT_ACTIVE)) != 0;
}

static bool	    is_alpha(int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int	    toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		return c - ('a' - 'A');
	return c;
}

char		    ps2_key_name_with_modifiers(struct ps2_keyboard_state *state, struct scan_key_code *key_id)
{
	const char  *has_shifted_value = "1234567890-=[]\\';/.,`";
	const char  *shifted_values =    "!@#$%^&*()_+{}|\":?><~";
	char	    c = 0x0; //default no-value value


	if (key_code_has_ascii_value(key_id)) {
		c = key_id->ascii_value;
	} else {
		return c;
	}
	if (ps2_is_shifted(state)) {
		if (is_alpha(c)) {
			c = toupper(c);
		} else if (strchr(has_shifted_value, c)) {
			uint32_t     index = strchr(has_shifted_value, c) - has_shifted_value;

			c = shifted_values[index];
		}
	}
	return c;
}
