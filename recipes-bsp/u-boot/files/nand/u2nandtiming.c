/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <common.h>
#include <div64.h>
#include <nand-timing.h>
struct metric { u64 ticks; unsigned bytes; };
static struct metric stats[2][H432B_METRICS];
static unsigned phase;
static int enabled;
int h432b_nand_timing_active(void) { return enabled; }
void h432b_nand_timing_reset(void)
{
 memset(stats, 0, sizeof(stats));
 phase = 0;
 enabled = 1;
}
void h432b_nand_timing_kernel(void) { phase = 1; }
void h432b_nand_timing_add(unsigned metric, u64 ticks, unsigned bytes)
{
 if (!enabled || metric >= H432B_METRICS) return;
 stats[phase][metric].ticks += ticks;
 stats[phase][metric].bytes += bytes;
}
static unsigned milliseconds(u64 ticks)
{
 /* Measurements are bounded to a boot attempt, not arbitrary uptime. */
 ticks *= 1000;
 do_div(ticks, get_tbclk());
 return (unsigned)ticks;
}
int h432b_nand_timing_format(char *buffer, unsigned size)
{
 unsigned p;
 int used = 0, n;
 enabled = 0;
 for (p = 0; p < 2; p++) {
  struct metric *m = stats[p];
  n = snprintf(buffer + used, size - used,
    " openh432.nand_%s=%u,%u,%u,%u,%u,%u,%u",
    p ? "load" : "scan", m[H432B_FIFO].bytes,
    milliseconds(m[H432B_FIFO].ticks),
    milliseconds(m[H432B_COMMAND].ticks),
    milliseconds(m[H432B_ENCODE].ticks),
    milliseconds(m[H432B_CORRECT].ticks),
    m[H432B_CRC].bytes, milliseconds(m[H432B_CRC].ticks));
  if (n < 0 || (unsigned)n >= size - used) return -1;
  used += n;
 }
 return used;
}
