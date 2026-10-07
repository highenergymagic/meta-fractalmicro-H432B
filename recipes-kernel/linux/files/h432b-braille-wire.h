/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef H432B_BRAILLE_WIRE_H
#define H432B_BRAILLE_WIRE_H
/* Standard dot mask -> physical shift byte, including alternating polarity.
 * Dots 1..6 follow the bootloader mapping; full eight-dot display output
 * and the dot-7 left-column extension were checked on an H432B.
 */
static inline unsigned char h432_braille_wire(unsigned char dots)
{
	unsigned char cell = ((dots & 0x01U) << 3) |
		((dots & 0x02U) << 1) | ((dots & 0x04U) >> 1) |
		((dots & 0x08U) << 4) | ((dots & 0x10U) << 2) |
		(dots & 0x20U) | ((dots & 0x40U) >> 6) |
		((dots & 0x80U) >> 3);
	return cell ^ 0x55U;
}
#endif
