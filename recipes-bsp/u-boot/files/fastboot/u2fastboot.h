/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef U2_FASTBOOT_H
#define U2_FASTBOOT_H
/* Pure, bounds-checked wire-format helpers shared by target and native tests. */
#define U2_FB_ADDRESS 0x48000000U
#define U2_FB_LIMIT   0x02000000U
#define U2_FB_PAGE    2048U
#define U2_FB_HEADER  1660U
struct u2_fb_image {
 unsigned kernel, ramdisk, dtb;
 unsigned kernel_size, ramdisk_size, dtb_size;
};
static inline unsigned u2_fb_le32(const unsigned char *p)
{
 return (unsigned)p[0] | (unsigned)p[1]<<8 |
        (unsigned)p[2]<<16 | (unsigned)p[3]<<24;
}
static inline int u2_fb_size(const unsigned char *p, unsigned n, unsigned *size)
{
 unsigned i, v=0, d;
 if (n!=8) return -1;
 for (i=0;i<n;i++) {
  if (p[i]>='0' && p[i]<='9') d=p[i]-'0';
  else if (p[i]>='a' && p[i]<='f') d=p[i]-'a'+10;
  else if (p[i]>='A' && p[i]<='F') d=p[i]-'A'+10;
  else return -1;
  v=(v<<4)|d;
 }
 if (!v || v>U2_FB_LIMIT) return -1;
 *size=v;
 return 0;
}
/* Never add untrusted lengths until both size and remaining span are bounded. */
static inline int u2_fb_span(unsigned total, unsigned *cursor, unsigned size,
                      unsigned limit, unsigned *offset)
{
 unsigned rounded;
 if (!size || size>limit || *cursor>total || size>total-*cursor) return -1;
 rounded=(size+U2_FB_PAGE-1)&~(U2_FB_PAGE-1);
 if (rounded>total-*cursor) return -1;
 *offset=*cursor;
 *cursor+=rounded;
 return 0;
}
static inline int u2_fb_layout(const unsigned char *p, unsigned total,
                        struct u2_fb_image *out)
{
 unsigned i, cursor=U2_FB_PAGE;
 const char magic[]="ANDROID!";
 if (total<U2_FB_PAGE || total>U2_FB_LIMIT) return -1;
 for (i=0;i<8;i++) if(p[i]!=(unsigned char)magic[i]) return -1;
 /* Android v2, no executable second stage or recovery overlay. */
 if (u2_fb_le32(p+40)!=2 || u2_fb_le32(p+36)!=U2_FB_PAGE ||
     u2_fb_le32(p+1644)!=U2_FB_HEADER || u2_fb_le32(p+24) ||
     u2_fb_le32(p+1632) || u2_fb_le32(p+1636) || u2_fb_le32(p+1640))
  return -1;
 /* Addresses are a board contract, not arbitrary host-selected pointers. */
 if (u2_fb_le32(p+12)!=0x42000000U ||
     u2_fb_le32(p+20)!=0x44400000U ||
     u2_fb_le32(p+1652)!=0x44000000U || u2_fb_le32(p+1656))
  return -1;
 /* Use the DTB's qualified command line, not an unreviewed header override. */
 for (i=64;i<576;i++) if(p[i]) return -1;
 for (i=608;i<1632;i++) if(p[i]) return -1;
 out->kernel_size=u2_fb_le32(p+8);
 out->ramdisk_size=u2_fb_le32(p+16);
 out->dtb_size=u2_fb_le32(p+1648);
 if (out->kernel_size<48 || out->dtb_size<40 ||
     u2_fb_span(total,&cursor,out->kernel_size,0x02000000,&out->kernel) ||
     u2_fb_span(total,&cursor,out->ramdisk_size,0x01000000,&out->ramdisk) ||
     u2_fb_span(total,&cursor,out->dtb_size,0x00100000,&out->dtb) ||
     cursor!=total) return -1;
 return 0;
}
#endif
