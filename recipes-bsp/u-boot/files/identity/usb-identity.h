/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_USB_IDENTITY_H
#define H432B_USB_IDENTITY_H

#define H432B_USB_MANUFACTURER "Fractal Microsystems"
#define H432B_USB_LINUX_PRODUCT "Braille Sense U2 (Linux)"
#define H432B_USB_MAINTENANCE_PRODUCT "Braille Sense U2 (Maintenance)"
#define H432B_USB_ID_PREFIX "FM-H432B-MAC-"
#define H432B_USB_ID_LENGTH (sizeof(H432B_USB_ID_PREFIX) - 1 + 12)
/* arch/arm/include/uapi/asm/setup.h: includes the terminating NUL. */
#define H432B_KERNEL_COMMAND_LINE_SIZE 1024

/* The serial string identifies a validated factory MAC, not a manufacturer
 * serial number. Callers pass NULL when the factory handoff is unavailable. */
static int h432b_usb_id_valid(const char *id)
{
	unsigned int i;

	if (!id)
		return 1;
	if (strlen(id) != H432B_USB_ID_LENGTH ||
	    strncmp(id, H432B_USB_ID_PREFIX, sizeof(H432B_USB_ID_PREFIX) - 1))
		return 0;
	for (i = sizeof(H432B_USB_ID_PREFIX) - 1; i < H432B_USB_ID_LENGTH; i++)
		if (!((id[i] >= '0' && id[i] <= '9') ||
		      (id[i] >= 'A' && id[i] <= 'F')))
			return 0;
	return 1;
}

static int h432b_usb_append(char *out, unsigned int capacity,
			   unsigned int *used, const char *text,
			   unsigned int length)
{
	unsigned int separator = *used != 0;

	if (*used >= capacity || separator >= capacity - *used ||
	    length >= capacity - *used - separator)
		return -1;
	if (separator)
		out[(*used)++] = ' ';
	memcpy(out + *used, text, length);
	*used += length;
	out[*used] = '\0';
	return 0;
}

/* Rewrite only the three gadget identity options in final kernel arguments.
 * Preserve other parameters, including slot/attempt accounting and options
 * containing quoted spaces. Insert before --, if an init argv suffix exists. */
static int h432b_usb_bootargs(const char *args, unsigned int size,
			    const char *id, char *out, unsigned int capacity)
{
	static const char names[][24] = {
		"g_serial.iManufacturer=", "g_serial.iProduct=",
		"g_serial.iSerialNumber=",
	};
	static const char identity[] =
		"g_serial.iManufacturer=\"" H432B_USB_MANUFACTURER "\" "
		"g_serial.iProduct=\"" H432B_USB_LINUX_PRODUCT "\"";
	const char *cursor = args, *tail = NULL;
	unsigned int used = 0;

	if (!size || !capacity || args[size - 1] ||
	    memchr(args, '\0', size - 1) || !h432b_usb_id_valid(id))
		return -1;
	out[0] = '\0';
	while (*cursor) {
		const char *start, *key;
		unsigned int i, length;
		int quoted = 0, skip = 0;

		while (*cursor == ' ' || *cursor == '\t')
			cursor++;
		if (!*cursor)
			break;
		start = cursor;
		while (*cursor && (quoted || (*cursor != ' ' && *cursor != '\t'))) {
			if (*cursor == '"')
				quoted = !quoted;
			cursor++;
		}
		if (quoted)
			return -1;
		length = cursor - start;
		if (length == 2 && !memcmp(start, "--", 2)) {
			tail = start;
			break;
		}
		key = start + (*start == '"');
		for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
			if (!strncmp(key, names[i], strlen(names[i])))
				skip = 1;
		if (!skip && h432b_usb_append(out, capacity, &used, start, length))
			return -1;
	}
	if (h432b_usb_append(out, capacity, &used, identity, sizeof(identity) - 1))
		return -1;
	if (id) {
		static const char serial[] = "g_serial.iSerialNumber=";
		char serial_option[sizeof(serial) + H432B_USB_ID_LENGTH];

		memcpy(serial_option, serial, sizeof(serial) - 1);
		memcpy(serial_option + sizeof(serial) - 1, id, H432B_USB_ID_LENGTH + 1);
		if (h432b_usb_append(out, capacity, &used, serial_option,
				    sizeof(serial_option) - 1))
			return -1;
	}
	if (tail && h432b_usb_append(out, capacity, &used, tail, strlen(tail)))
		return -1;
	return 0;
}
#endif
