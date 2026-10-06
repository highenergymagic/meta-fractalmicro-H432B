/* SPDX-License-Identifier: GPL-2.0-or-later */
#define U2_TIMER_TEST 1
#include "u2timer.c"
int main(void)
{
 unsigned con=0x000abcdeU, mux=0x12345678U, before, start;
 memset(clocks,0,sizeof clocks); memset(pwm,0,sizeof pwm);
 clocks[0x200/4]=(1U<<4)|1;
 clocks[0x300/4]=(4U<<24)|(1U<<28);
 pwm[0]=0x00000753U; pwm[1]=mux; pwm[2]=con;
 pwm[17]=0x1f; step=1;
 assert(input_rate()==66700000UL);
 assert(timer_init()==0);
 assert(gd->timer_rate_hz==521093UL);
 assert(pwm[0]==0x00000753U);
 assert((pwm[1]&~(15U<<16))==(mux&~(15U<<16)));
 assert((pwm[2]&~(7U<<20))==(con&~(7U<<20)));
 assert(pwm[17]==0x20f);
 assert(clocks[0x46c/4]==(1U<<23));
 step=0; gd->lastinc=2; gd->tbl=0; gd->tbu=0; pwm[16]=0xfffffffeU;
 assert(get_ticks()==4);
 gd->lastinc=20; gd->tbl=0xfffffffeU; gd->tbu=7; pwm[16]=16;
 assert(get_ticks()==((8ULL<<32)|2));
 gd->timer_rate_hz=1000000; gd->lastinc=pwm[16]; gd->tbl=2000000; gd->tbu=0;
 assert(get_timer(0)==2000); assert(get_timer(1990)==10);
 start=pwm[16]; step=1; __udelay(0); assert(pwm[16]==start);
 __udelay(1); assert((unsigned)(start-pwm[16])>=2);
 before=writes; step=0; assert(timer_init()==-1); assert(writes>before);
 clocks[0x200/4]=0; assert(input_rate()==2400000);
 clocks[0x200/4]=(1U<<24)|1; clocks[0x300/4]=(3U<<4);
 assert(input_rate()==200000000UL);
 assert(do_u2timer(NULL,0,1,NULL)==CMD_RET_USAGE);
 puts("PWM4 mock tests passed: mux, isolation, rollover, units, delays, stopped timer");
 return 0;
}
