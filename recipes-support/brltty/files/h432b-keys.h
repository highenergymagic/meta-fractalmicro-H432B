/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef H432B_KEYS_H
#define H432B_KEYS_H
/* HIMS key numbers; routing is a separate group. */
static int h432_key_number(unsigned int routing, unsigned int code)
{
	if (routing)
		return code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY32 ?
			(int)(code - BTN_TRIGGER_HAPPY1) : -1;
	if (code >= KEY_BRL_DOT1 && code <= KEY_BRL_DOT6)
		return code - KEY_BRL_DOT1;
	if (code >= KEY_F1 && code <= KEY_F4)
		return 9 + code - KEY_F1;
	if (code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY4)
		return 16 + code - BTN_TRIGGER_HAPPY1;
	if (code >= BTN_0 && code <= BTN_4)
		return 32 + code - BTN_0;
	switch (code) {
	case KEY_BACKSPACE: return 6;
	case KEY_ENTER: return 7;
	case KEY_SPACE: return 8;
	default: return -1;
	}
}
#endif
