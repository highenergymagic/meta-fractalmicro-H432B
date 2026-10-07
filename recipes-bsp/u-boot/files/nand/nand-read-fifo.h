/* SPDX-License-Identifier: GPL-2.0-or-later */
/* S5PV210 little-endian FIFO: CPU access width is independent of NAND bus width.
 * Command/status/ID byte callbacks are deliberately not replaced.
 */
static void u2_nand_read_fifo(u8 *buf, int len)
{
 while (len > 0 && ((unsigned long)buf & 3)) {
  *buf++ = U2_NAND_READ8();
  len--;
 }
 while (len >= 4) {
  *(u32 *)buf = U2_NAND_READ32();
  buf += 4;
  len -= 4;
 }
 while (len-- > 0)
  *buf++ = U2_NAND_READ8();
}
