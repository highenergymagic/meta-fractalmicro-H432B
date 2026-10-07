/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
static unsigned cursor, words, bytes;
static u8 sample(unsigned n) { return (u8)(n * 37 + (n >> 8)); }
static u8 read8(void) { bytes++; return sample(cursor++); }
static u32 read32(void) {
 u32 v = 0;
 unsigned i;
 words++;
 for (i = 0; i < 4; i++) v |= (u32)sample(cursor++) << (8 * i);
 return v;
}
#define U2_NAND_READ8() read8()
#define U2_NAND_READ32() read32()
#include "nand-read-fifo.h"
int main(void)
{
 union { u32 align; u8 data[4120]; } buffer;
 unsigned offset, len, i;
 for (offset = 4; offset < 8; offset++) {
  for (len = 0; len <= 4096; len++) {
   memset(buffer.data, 0xa5, sizeof(buffer.data));
   cursor = words = bytes = 0;
   u2_nand_read_fifo(buffer.data + offset, (int)len);
   assert(cursor == len);
   assert(words * 4 + bytes == len);
   for (i = 0; i < offset; i++) assert(buffer.data[i] == 0xa5);
   for (i = 0; i < len; i++) assert(buffer.data[offset+i] == sample(i));
   for (i = offset+len; i < sizeof(buffer.data); i++)
    assert(buffer.data[i] == 0xa5);
   if (!(offset & 3) && !(len & 3)) assert(bytes == 0);
  }
 }
 cursor = words = bytes = 0;
 u2_nand_read_fifo(buffer.data, -1);
 assert(cursor == 0);
 puts("NAND FIFO alignment, tails, byte order and bounds: PASS");
 return 0;
}
