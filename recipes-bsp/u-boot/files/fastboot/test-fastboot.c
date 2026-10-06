/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "u2fastboot.h"
static unsigned char p[2048];
static void set(unsigned at,unsigned n)
{
 p[at]=n; p[at+1]=n>>8; p[at+2]=n>>16; p[at+3]=n>>24;
}
static void baseline(void)
{
 memset(p,0,sizeof(p));
 memcpy(p,"ANDROID!",8);
 set(8,48); set(12,0x42000000); set(16,123);
 set(20,0x44400000); set(36,2048); set(40,2);
 set(1644,1660); set(1648,40); set(1652,0x44000000);
}
int main(void)
{
 struct u2_fb_image b;
 unsigned n,i,c,o;
 assert(!u2_fb_size((const unsigned char *)"02000000",8,&n));
 assert(n==U2_FB_LIMIT);
 assert(u2_fb_size((const unsigned char *)"00000000",8,&n));
 assert(u2_fb_size((const unsigned char *)"02000001",8,&n));
 assert(u2_fb_size((const unsigned char *)"ffffffff",8,&n));
 assert(u2_fb_size((const unsigned char *)"0000000g",8,&n));
 assert(u2_fb_size((const unsigned char *)"00000001",7,&n));
 baseline();
 assert(!u2_fb_layout(p,8192,&b));
 assert(b.kernel==2048 && b.ramdisk==4096 && b.dtb==6144);
 for(i=0;i<8192;i++) assert(u2_fb_layout(p,i,&b));
 assert(u2_fb_layout(p,8193,&b));
 for(i=0;i<3;i++) {
  baseline(); set(i==0?8:i==1?16:1648,0xffffffff);
  assert(u2_fb_layout(p,8192,&b));
 }
 baseline(); set(12,0x46000000); assert(u2_fb_layout(p,8192,&b));
 baseline(); set(40,0); assert(u2_fb_layout(p,8192,&b));
 baseline(); set(36,4096); assert(u2_fb_layout(p,8192,&b));
 baseline(); set(24,1); assert(u2_fb_layout(p,8192,&b));
 baseline(); set(1632,1); assert(u2_fb_layout(p,8192,&b));
 baseline(); set(1656,1); assert(u2_fb_layout(p,8192,&b));
 baseline(); p[64]='a'; assert(u2_fb_layout(p,8192,&b));
 baseline(); p[608]='b'; assert(u2_fb_layout(p,8192,&b));
 c=U2_FB_LIMIT; assert(u2_fb_span(U2_FB_LIMIT,&c,1,1024,&o));
 c=0xffffffff; assert(u2_fb_span(U2_FB_LIMIT,&c,1,1024,&o));
 puts("fastboot parser tests passed");
 return 0;
}
