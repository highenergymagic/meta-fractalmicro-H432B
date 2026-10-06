// SPDX-License-Identifier: GPL-2.0-or-later
/* H432B physical-address PWM4 clocksource. No output pin or IRQ is used. */
#ifdef U2_TIMER_TEST
#include "timer-test-shim.h"
#else
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include <asm/arch/clk.h>
#include <div64.h>
#endif

DECLARE_GLOBAL_DATA_PTR;
#define CLK(o) ((void *)(0xe0100000U + (o)))
#define PWM(o) ((void *)(0xe2500000U + (o)))
#define START (1U << 20)
#define UPDATE (1U << 21)
#define RELOAD (1U << 22)

/* CLK_SRC0 PSYS selects MPLL or APLL/A2M; retain all clock-tree settings.
 * The board crystal is 24 MHz. Upstream get_pwm_clk ignores this mux. */
static unsigned long input_rate(void)
{
 unsigned src=readl(CLK(0x200)), div=readl(CLK(0x300));
 unsigned long rate;
 if(src & (1U<<24)) {
  rate=(src&1) ? get_pll_clk(APLL) : 24000000UL;
  rate/=((div>>4)&7)+1;
 } else {
  rate=(src&(1U<<4)) ? get_pll_clk(MPLL) : 24000000UL;
 }
 rate/=((div>>24)&15)+1;
 rate/=((div>>28)&7)+1;
 return rate;
}

void reset_timer_masked(void)
{
 gd->lastinc=readl(PWM(0x40));
 gd->tbl=0;
 gd->tbu=0;
}

/* State lives in gd, not BSS: it must survive board_init_f -> board_init_r. */
unsigned long long get_ticks(void)
{
 unsigned now=readl(PWM(0x40));
 unsigned delta=(unsigned)(gd->lastinc-now);
 unsigned old=(unsigned)gd->tbl;
 gd->lastinc=now;
 gd->tbl=(unsigned)(old+delta);
 if((unsigned)gd->tbl<old) gd->tbu++;
 return ((u64)gd->tbu<<32)|(unsigned)gd->tbl;
}
unsigned long get_current_tick(void) { return (unsigned long)get_ticks(); }
unsigned long get_tbclk(void) { return gd->timer_rate_hz; }
unsigned long get_timer_masked(void)
{
 u64 ticks=get_ticks();
 /* Divide before multiply to keep extended uptime conversions bounded. */
 unsigned rem=do_div(ticks,gd->timer_rate_hz);
 u64 fraction=(u64)rem*CONFIG_SYS_HZ;
 do_div(fraction,gd->timer_rate_hz);
 return (unsigned long)(ticks*CONFIG_SYS_HZ+fraction);
}
unsigned long get_timer(unsigned long base) { return get_timer_masked()-base; }

void __udelay(unsigned long usec)
{
 while(usec) {
  unsigned chunk=usec>1000000UL ? 1000000U : (unsigned)usec;
  u64 ticks=(u64)chunk*gd->timer_rate_hz+999999U;
  unsigned start=readl(PWM(0x40));
  do_div(ticks,1000000U);
  /* One extra tick covers sampling immediately before a clock edge. */
  ticks++;
  while((unsigned)(start-readl(PWM(0x40)))<(unsigned)ticks)
   ;
  get_ticks();
  usec-=chunk;
 }
}

int timer_init(void)
{
 unsigned con, previous, i, prescale;
 unsigned long rate=input_rate();
 if(rate<1000000UL || rate>200000000UL) return -1;
 /* Enable only the PWM gate. Preserve shared prescaler and timers 0..3. */
 writel(readl(CLK(0x46c))|(1U<<23),CLK(0x46c));
 readl(CLK(0x46c));
 prescale=((readl(PWM(0))>>8)&255)+1;
 gd->timer_rate_hz=rate/prescale/16;
 if(!gd->timer_rate_hz) return -1;
 con=readl(PWM(8))&~(START|UPDATE|RELOAD);
 writel(con,PWM(8));
 /* TINT_CSTAT: preserve IRQ enables 0..3; acknowledge only channel 4. */
 writel((readl(PWM(0x44))&15U)|(1U<<9),PWM(0x44));
 writel((readl(PWM(4))&~(15U<<16))|(4U<<16),PWM(4));
 writel(0xffffffffU,PWM(0x3c));
 writel(con|UPDATE,PWM(8));
 writel(con|START|RELOAD,PWM(8));
 previous=readl(PWM(0x40));
 /* Bounded liveness check; do not use the clock to time its own failure. */
 for(i=0;i<1000000U;i++)
  if(readl(PWM(0x40))!=previous) break;
 if(i==1000000U) return -1;
 reset_timer_masked();
 return 0;
}

/* Host-time a long delay before trusting any NAND timeout measurement. */
static int do_u2timer(cmd_tbl_t *cmdtp,int flag,int argc,char *const argv[])
{
 unsigned long begin;
 if(argc!=2) return CMD_RET_USAGE;
 if(!strcmp(argv[1],"status")) {
  printf("PWM4 rate=%lu Hz counter=%08x elapsed=%lu ms icache=%d\n",
         gd->timer_rate_hz,readl(PWM(0x40)),get_timer(0),icache_status());
  return 0;
 }
 if(strcmp(argv[1],"test")) return CMD_RET_USAGE;
 begin=get_timer(0);
 __udelay(5000000UL);
 printf("PWM4 five-second delay: %lu ms (verify with host clock)\n",get_timer(begin));
 return 0;
}
U_BOOT_CMD(u2timer,2,0,do_u2timer,"inspect hardware clock and test delay","status|test");
