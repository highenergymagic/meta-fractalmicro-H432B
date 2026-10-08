/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_NAND_TIMING_H
#define H432B_NAND_TIMING_H
enum h432b_nand_metric { H432B_FIFO, H432B_COMMAND, H432B_ENCODE,
                        H432B_CORRECT, H432B_CRC, H432B_METRICS };
int h432b_nand_timing_active(void);
void h432b_nand_timing_reset(void);
void h432b_nand_timing_kernel(void);
void h432b_nand_timing_add(unsigned metric, unsigned long long ticks, unsigned bytes);
int h432b_nand_timing_format(char *buffer, unsigned size);
#endif
