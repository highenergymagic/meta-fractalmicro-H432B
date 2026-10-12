// SPDX-License-Identifier: GPL-2.0-or-later
/* CPU-only NAND boot interval; USB DMA never runs with cached RAM. */
#include <common.h>
#include <command.h>
#include <asm/system.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

extern int u2probe_start(void);
extern int u2probe_stop(void);

static int cache_owned;
static unsigned long saved_control, saved_ttbcr;

/* Generic 2012.10 section descriptors assume TEX remapping and access-flag
 * interpretation are disabled, extended page tables and TTBR0 cover all VA.
 * Establish those assumptions only with translation off; preserve the rest
 * of SCTLR, including the instruction cache and branch-predictor policy.
 */
#define H432B_MMU_ATTRIBUTES ((1UL << 28) | (1UL << 29) | (1UL << 23))

#ifndef H432B_CACHE_NATIVE_TEST
static unsigned long read_ttbcr(void)
{
	unsigned long value;

	asm volatile("mrc p15, 0, %0, c2, c0, 2" : "=r" (value));
	return value;
}

static void write_ttbcr(unsigned long value)
{
	asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r" (value) : "memory");
}

static void cache_sync(void)
{
	asm volatile("dsb sy\n\tisb sy" : : : "memory");
}

static int cache_dma_idle(void)
{
	unsigned long start = get_timer(0);

	/* DWC2 GRSTCTL.AHBIDLE: wait for any in-flight bus transaction. */
	while (!(readl(0xec000010) & (1UL << 31))) {
		if (get_timer(start) >= 100)
			return 0;
		udelay(1);
	}
	return 1;
}
#endif

int h432b_nand_cache_active(void)
{
	return cache_owned;
}

void h432b_nand_cache_leave(void)
{
	unsigned long control;

	if (!cache_owned)
		return;
	/* Upstream implementation cleans all data before clearing C and M. */
	dcache_disable();
	invalidate_dcache_all();
	cache_sync();
	write_ttbcr(saved_ttbcr);
	control = get_cr();
	set_cr((control & ~H432B_MMU_ATTRIBUTES) |
	       (saved_control & H432B_MMU_ATTRIBUTES));
	cache_sync();
	cache_owned = 0;
}

static int do_u2cachedboot(cmd_tbl_t *cmdtp, int flag, int argc,
			 char *const argv[])
{
	int result;

	if (argc != 1)
		return CMD_RET_USAGE;
	/* The normal relocation allocator reserves this 16 KiB table below
	 * CONFIG_U2_ALLOC_TOP, outside the heap and all kernel upload buffers.
	 */
	if (cache_owned || (get_cr() & (CR_M | CR_C)) ||
	    gd->tlb_addr != CONFIG_U2_ALLOC_TOP - 0x10000 ||
	    gd->bd->bi_dram[0].start != CONFIG_SYS_SDRAM_BASE ||
	    gd->bd->bi_dram[0].size != CONFIG_SYS_SDRAM_SIZE) {
		puts("Cached NAND boot refused: unexpected MMU or RAM layout\n");
		return CMD_RET_FAILURE;
	}
	/* Reinitialize inherited controller state before disconnecting it.
	 * In particular, inactive software state is not proof DMA is stopped.
	 */
	if (u2probe_start() || u2probe_stop())
		return CMD_RET_FAILURE;
	if (!cache_dma_idle()) {
		puts("Cached NAND boot refused: USB DMA is not idle\n");
		return CMD_RET_FAILURE;
	}
	saved_control = get_cr();
	saved_ttbcr = read_ttbcr();
	write_ttbcr(0);
	set_cr((saved_control & ~H432B_MMU_ATTRIBUTES) | (1UL << 23));
	cache_sync();
	/* Upstream maps only advertised DRAM cacheable, all MMIO uncached,
	 * and invalidates data caches and TLBs before enabling translation.
	 */
	dcache_enable();
	cache_sync();
	cache_owned = 1;
	result = run_command("u2bootstate boot", 0);
	/* Success does not return. Every failure restores the USB contract. */
	h432b_nand_cache_leave();
	return result;
}

U_BOOT_CMD(u2cachedboot, 1, 0, do_u2cachedboot,
	   "boot NAND with cached CPU memory and USB DMA stopped", "");
