/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint64_t u64;
typedef int cmd_tbl_t;
typedef struct { unsigned long lastinc, tbl, tbu, timer_rate_hz; } test_gd;
static test_gd state;
#define DECLARE_GLOBAL_DATA_PTR static test_gd *gd=&state
#define CONFIG_SYS_HZ 1000
#define CMD_RET_USAGE -1
#define U_BOOT_CMD(...)
#define APLL 0
#define MPLL 1
static unsigned clocks[512], pwm[18], step, writes;
static unsigned long get_pll_clk(int pll) { return pll==APLL?800000000UL:667000000UL; }
static unsigned readl(const void *addr)
{
 uintptr_t a=(uintptr_t)addr;
 if(a>=0xe2500000U && a<=0xe2500044U) {
  unsigned value=pwm[(a-0xe2500000U)/4];
  if(a==0xe2500040U) pwm[16]-=step;
  return value;
 }
 assert(a>=0xe0100000U && a<0xe0100800U);
 return clocks[(a-0xe0100000U)/4];
}
static void writel(unsigned value,void *addr)
{
 uintptr_t a=(uintptr_t)addr;
 writes++;
 if(a>=0xe2500000U && a<=0xe2500044U) {
  pwm[(a-0xe2500000U)/4]=value;
  if(a==0xe2500008U && (value&(1U<<21))) pwm[16]=pwm[15];
 } else {
  assert(a==0xe010046cU);
  clocks[(a-0xe0100000U)/4]=value;
 }
}
static int icache_status(void) { return 0; }
static unsigned test_div(u64 *n,unsigned d) { unsigned r=*n%d;*n/=d;return r; }
#define do_div(n,d) test_div(&(n),(d))
