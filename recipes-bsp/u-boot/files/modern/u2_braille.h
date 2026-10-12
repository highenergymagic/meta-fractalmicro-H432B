/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef U2_BRAILLE_H
#define U2_BRAILLE_H

/* Recovered from CE brlcell.dll, C0E320F8. Transport only: no mux,
 * regulator, Cell_On, PMIC, NAND or clock-controller writes. */
#define U2_GPJ0CON 0xe0200240UL
#define U2_GPJ0DAT 0xe0200244UL
#define U2_GPJ1CON 0xe0200260UL
#define U2_GPJ1DAT 0xe0200264UL
#define U2_GPJ4CON 0xe02002c0UL
#define U2_GPJ4DAT 0xe02002c4UL
#define U2_BRL_DATA  0x20U
#define U2_BRL_CLOCK 0x10U
#define U2_BRL_LATCH 0x02U

#ifndef U2_BRL_READ
#define U2_BRL_READ(addr) (*(volatile unsigned int *)(addr))
#define U2_BRL_WRITE(addr, value) do { \
    *(volatile unsigned int *)(addr) = (value); \
    __asm__ volatile("dsb sy" : : : "memory"); \
    (void)U2_BRL_READ(addr); \
} while (0)
#endif

/* Exact contracted Braille ASCII supplied by the user. These strings
 * already contain the translation: do NOT run a text translator on them.
 * Letter case is folded only when decoding six-dot Braille ASCII. */
#define U2_BRAILLE_BRAND_BRF ",Fractal ,Microsy/ems"
#define U2_BRAILLE_KERNEL_BRF ",/>t+ ,open;,h#dcb444"

/* North American Braille ASCII, characters 0x20..0x5f -> standard dots.
 * Dot-number mapping checked against the primary liblouis display table:
 * https://github.com/liblouis/liblouis/blob/master/tables/en-us-brf.dis
 * This is a display-code decoder, NOT a Grade 2 translator. */
static const unsigned char u2_brf_dots[64] = {
    0x00, 0x2e, 0x10, 0x3c, 0x2b, 0x29, 0x2f, 0x04,
    0x37, 0x3e, 0x21, 0x2c, 0x20, 0x24, 0x28, 0x0c,
    0x34, 0x02, 0x06, 0x12, 0x32, 0x22, 0x16, 0x36,
    0x26, 0x14, 0x31, 0x30, 0x23, 0x3f, 0x1c, 0x39,
    0x08, 0x01, 0x03, 0x09, 0x19, 0x11, 0x0b, 0x1b,
    0x13, 0x0a, 0x1a, 0x05, 0x07, 0x0d, 0x1d, 0x15,
    0x0f, 0x1f, 0x17, 0x0e, 0x1e, 0x25, 0x27, 0x3a,
    0x2d, 0x3d, 0x35, 0x2a, 0x33, 0x3b, 0x18, 0x38
};

/* StepLoader bootimage.nb0 +0x7c0 emits the readable HIMS greeting using
 * these CE-format bytes: H=4c I=84 M=8a S=86 a=08 l=0e cap=20.
 * Together they determine dots 1..6 = 08 04 02 80 40 20. All remaining
 * greeting symbols agree. Dot 7/8 mapping is NOT yet established. */
static unsigned char u2_braille_sixdot_cell(unsigned char dots)
{
    return ((dots & 0x01U) << 3) | ((dots & 0x02U) << 1) |
           ((dots & 0x04U) >> 1) | ((dots & 0x08U) << 4) |
           ((dots & 0x10U) << 2) | (dots & 0x20U);
}

/* Internal transport accepts CE-format bytes, NOT standard dot masks. */
static int u2_braille_ce_frame(const unsigned char frame[32])
{
    int cell, bit;
    unsigned int value, wire;

    /* Only reuse a live EBOOT-configured display; do not enable one. */
    if ((U2_BRL_READ(U2_GPJ0CON) & 0xf000U) != 0x1000U ||
        (U2_BRL_READ(U2_GPJ1CON) & 0xff0000U) != 0x110000U ||
        (U2_BRL_READ(U2_GPJ4CON) & 0xf0U) != 0x10U)
        return 1;
    if (!(U2_BRL_READ(U2_GPJ0DAT) & 8U))
        return 2;
    if (U2_BRL_READ(U2_GPJ1DAT) & U2_BRL_CLOCK)
        return 3;

    value = U2_BRL_READ(U2_GPJ4DAT) & ~U2_BRL_LATCH;
    U2_BRL_WRITE(U2_GPJ4DAT, value);
    for (cell = 31; cell >= 0; --cell) {
        /* CE reverses the polarity at odd MSB-first bit indices. */
        wire = frame[cell] ^ 0x55U;
        for (bit = 7; bit >= 0; --bit) {
            value = U2_BRL_READ(U2_GPJ1DAT) & ~U2_BRL_DATA;
            if (wire & (1U << bit))
                value |= U2_BRL_DATA;
            U2_BRL_WRITE(U2_GPJ1DAT, value);
            value = U2_BRL_READ(U2_GPJ1DAT) | U2_BRL_CLOCK;
            U2_BRL_WRITE(U2_GPJ1DAT, value);
            value = U2_BRL_READ(U2_GPJ1DAT) & ~U2_BRL_CLOCK;
            U2_BRL_WRITE(U2_GPJ1DAT, value);
        }
    }
    value = U2_BRL_READ(U2_GPJ4DAT) | U2_BRL_LATCH;
    U2_BRL_WRITE(U2_GPJ4DAT, value);
    value = U2_BRL_READ(U2_GPJ4DAT) & ~U2_BRL_LATCH;
    U2_BRL_WRITE(U2_GPJ4DAT, value);
    return 0;
}

static int u2_braille_sixdot_frame(const unsigned char dots[32])
{
    unsigned char frame[32];
    unsigned int cell;

    for (cell = 0; cell < 32; ++cell) {
        if (dots[cell] & 0xc0U)
            return 5; /* No guessed dot-7/dot-8 mapping; no GPIO writes. */
        frame[cell] = u2_braille_sixdot_cell(dots[cell]);
    }
    return u2_braille_ce_frame(frame);
}

/* Reject unsupported/overlength input before any GPIO writes. Blank-pad
 * all unused cells so a shorter status replaces the previous full line. */
static int u2_braille_brf_decode(const char *brf, unsigned char dots[32])
{
    unsigned int cell = 0;
    unsigned char ch;

    while (*brf) {
        if (cell == 32)
            return 7;
        ch = (unsigned char)*brf++;
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (ch < 0x20 || ch > 0x5f)
            return 6;
        dots[cell++] = u2_brf_dots[ch - 0x20];
    }
    while (cell < 32)
        dots[cell++] = 0;
    return 0;
}

static int u2_braille_brf(const char *brf)
{
    unsigned char dots[32];
    int result = u2_braille_brf_decode(brf, dots);

    if (result)
        return result;
    return u2_braille_sixdot_frame(dots);
}
#endif
