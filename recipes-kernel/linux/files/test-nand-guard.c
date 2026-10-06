/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hims-u2-nand-guard.h"
static const unsigned first=0x1fee0000;
static struct u2_nand_guard blank(void)
{
 struct u2_nand_guard s={0};
 s.first=first; s.end=first+U2_ERASE_BYTES;
 return s;
}
static int address(struct u2_nand_guard *s,unsigned row,unsigned col,int erase)
{
 unsigned char a[5]={col,col>>8,row,row>>8,row>>16};
 return u2_guard_address(s,a+(erase?2:0),erase?3:5);
}
int main(void)
{
 struct u2_nand_guard s=blank();
 unsigned i;
 assert(u2_guard_window(first,U2_ERASE_BYTES));
 assert(!u2_guard_window(0,U2_ERASE_BYTES));
 assert(!u2_guard_window(U2_BOOT_END-1,U2_ERASE_BYTES));
 assert(!u2_guard_window(U2_BBT_START,U2_ERASE_BYTES));
 assert(!u2_guard_window(U2_BOOT_END,0xffffffff));
 assert(u2_guard_command(&s,0x10));
 assert(u2_guard_command(&s,0xd0));
 assert(u2_guard_output(&s,1));
 assert(!u2_guard_command(&s,0x80));
 assert(address(&s,0,0,0));
 s=blank(); assert(!u2_guard_command(&s,0x80));
 assert(address(&s,U2_NAND_BYTES/U2_PAGE_BYTES,0,0));
 s=blank(); assert(!u2_guard_command(&s,0x80));
 assert(!address(&s,first/U2_PAGE_BYTES,0,0));
 assert(!u2_guard_output(&s,2048)); assert(!u2_guard_output(&s,64));
 assert(u2_guard_output(&s,1)); assert(!u2_guard_command(&s,0x10));
 s=blank(); assert(!u2_guard_command(&s,0x60));
 assert(address(&s,first/U2_PAGE_BYTES+1,0,1));
 s=blank(); assert(!u2_guard_command(&s,0x60));
 assert(!address(&s,first/U2_PAGE_BYTES,0,1));
 assert(!u2_guard_command(&s,0xd0));
 for(i=0;i<U2_NAND_BYTES/U2_PAGE_BYTES;i+=64) {
  s=blank(); assert(!u2_guard_command(&s,0x60));
  assert((address(&s,i,0,1)==0)==(i==first/U2_PAGE_BYTES));
 }
 s=blank(); assert(!u2_guard_command(&s,0x80));
 assert(!address(&s,first/U2_PAGE_BYTES,0,0));
 assert(!u2_guard_command(&s,0x00));
 assert(u2_guard_output(&s,1));
 assert(u2_guard_command(&s,0x85)); /* random data input is not implemented */
 puts("NAND transaction guard: all4096 eraseblocks checked");
 return 0;
}
