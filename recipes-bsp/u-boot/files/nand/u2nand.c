// SPDX-License-Identifier: GPL-2.0-or-later
/* H432B Linux-format NAND reader. No program/erase commands reach NAND. */
#include <common.h>
#include <command.h>
#include <nand.h>
#include <errno.h>
#include <asm/io.h>
#include "u2fastboot.h"

#define REG(o) ((void __iomem *)(0xb0e00000U+(o)))
static struct nand_chip u2_chip;
static int blocked;
static unsigned saved_cont;
static int initialized, identified;
extern int u2probe_is_active(void);
extern int u2probe_start(void);
extern int usb_gadget_handle_interrupts(void);
extern int u2_fastboot_linux(const struct u2_fb_image *);
extern int ubi_volume_read_bounded(char *, void *, size_t, size_t *);

static int read_command(int cmd)
{
 switch(cmd) {
 case 0x00: case 0x05: case 0x30: case 0x70: case 0x90:
 case 0xe0: case 0xec: case 0xee: case 0xff: return 1;
 default: return 0;
 }
}
static void control(struct mtd_info *mtd,int cmd,unsigned ctrl)
{
 if(ctrl&NAND_CTRL_CHANGE) {
  unsigned v=readl(REG(4));
  if(ctrl&NAND_NCE) v=(v&~2U)|0xc0U;
  else v=saved_cont|2U;
  writel(v,REG(4));
 }
 if(cmd==NAND_CMD_NONE) return;
 if(ctrl&NAND_CLE) {
  blocked=!read_command(cmd);
  if(!blocked) writeb(cmd,REG(8));
 } else if((ctrl&NAND_ALE) && !blocked) writeb(cmd,REG(12));
}
static int ready(struct mtd_info *mtd) {
 if(u2probe_is_active()) usb_gadget_handle_interrupts();
 return !!(readl(REG(0x28))&1);
}
static void no_write_buf(struct mtd_info *mtd,const u8 *buf,int len) { blocked=1; }
static int ro_write(struct mtd_info *mtd,loff_t off,size_t len,size_t *done,const u8 *buf)
{ *done=0; return -EROFS; }
static int ro_oob(struct mtd_info *mtd,loff_t off,struct mtd_oob_ops *ops)
{ ops->retlen=ops->oobretlen=0; return -EROFS; }
static int ro_erase(struct mtd_info *mtd,struct erase_info *e) { return -EROFS; }
static int ro_bad(struct mtd_info *mtd,loff_t off) { return -EROFS; }

/* Keep the USB console available before NAND qualification. */
void board_nand_init(void) { }
static int do_u2nandinit(cmd_tbl_t *cmdtp,int flag,int argc,char *const argv[])
{
 struct mtd_info *mtd=&nand_info[0];
 struct nand_chip *n=&u2_chip;
 int ret;
 if(argc!=2) return CMD_RET_USAGE;
 if(initialized) { puts("NAND already registered read-only\n"); return 0; }
 if(!strcmp(argv[1],"ident")) {
  if(identified) return 0;
  saved_cont=readl(REG(4));
  printf("NAND regs: CONF=%08x CONT=%08x STAT=%08x\n",
         readl(REG(0)),saved_cont,readl(REG(0x28)));
  if(!(saved_cont&1) || (readl(REG(0))&1)) return CMD_RET_FAILURE;
  mtd->priv=n;
  n->IO_ADDR_R=n->IO_ADDR_W=REG(0x10);
  n->cmd_ctrl=control; n->dev_ready=ready;
  n->write_buf=no_write_buf;
  n->options=NAND_NO_SUBPAGE_WRITE|NAND_SKIP_BBTSCAN;
  n->ecc.mode=NAND_ECC_SOFT_BCH; n->ecc.size=512; n->ecc.bytes=13;
  n->chip_delay=50;
  ret=nand_scan_ident(mtd,1,NULL);
  printf("NAND ident result=%d size=%llu page=%u oob=%u erase=%u\n",ret,
         mtd->size,mtd->writesize,mtd->oobsize,mtd->erasesize);
  if(ret || mtd->size!=0x20000000ULL || mtd->writesize!=2048 ||
     mtd->oobsize!=64 || mtd->erasesize!=131072) return CMD_RET_FAILURE;
  identified=1;
  return 0;
 }
 if(strcmp(argv[1],"ecc") || !identified) return CMD_RET_USAGE;
 puts("NAND software BCH initialization\n");
 ret=nand_scan_tail(mtd);
 printf("NAND tail result=%d\n",ret);
 if(ret) return CMD_RET_FAILURE;
 mtd->flags &= ~MTD_WRITEABLE;
 mtd->write=ro_write; mtd->panic_write=ro_write; mtd->write_oob=ro_oob;
 mtd->erase=ro_erase; mtd->block_markbad=ro_bad;
 nand_register(0);
 initialized=1;
 puts("H432B NAND BCH8/512: read-only, direct bad-block markers\n");
 return 0;
}
U_BOOT_CMD(u2nandinit,2,0,do_u2nandinit,"qualify NAND initialization","ident|ecc");

static int do_u2nandboot(cmd_tbl_t *cmdtp,int flag,int argc,char *const argv[])
{
 struct u2_fb_image image;
 size_t actual=0;
 char *volume;
 if(argc!=2 || !initialized) return CMD_RET_USAGE;
 if(!strcmp(argv[1],"a")) volume="kernel_a";
 else if(!strcmp(argv[1],"b")) volume="kernel_b";
 else return CMD_RET_USAGE;
 /* Initialize our own gadget so the proven Linux handoff can quiesce it,
  * including a second-stage launch inheriting NAND51 USB DMA state. */
 if(!u2probe_is_active() && u2probe_start()) return CMD_RET_FAILURE;
 /* ENV_IS_NOWHERE: defaults are compiled, but not loaded by mtdparts_init. */
 if(run_command("mtdparts default",0) ||
    run_command("ubi part linux",0)) return CMD_RET_FAILURE;
 if(ubi_volume_read_bounded(volume,(void *)U2_FB_ADDRESS,
                             132U*126976U,&actual) ||
    u2_fb_layout((const unsigned char *)U2_FB_ADDRESS,actual,&image)) {
  puts("NAND boot refused: unreadable/incomplete/invalid static kernel volume\n");
  return CMD_RET_FAILURE;
 }
 printf("NAND kernel %s: %u bytes, CRC32 %08x\n",volume,(unsigned)actual,
        crc32(0,(void *)U2_FB_ADDRESS,actual));
 return u2_fastboot_linux(&image);
}
U_BOOT_CMD(u2nandboot,2,0,do_u2nandboot,"boot a checked NAND kernel volume","a|b");

/* Inject errors only into RAM copies of the first scratch page. NAND is never
 * programmed here. Verify the Linux/U-Boot parity contract and 1..8-bit repair. */
static int do_u2nandcheck(cmd_tbl_t *cmdtp,int flag,int argc,char *const argv[])
{
 struct mtd_info *mtd=&nand_info[0];
 struct nand_chip *n=mtd->priv;
 struct mtd_oob_ops ops;
 u8 original[2048],oob[64],data[512],parity[13];
 unsigned step,bits,i;
 int ret;
 if(argc!=1 || !initialized) return CMD_RET_USAGE;
 memset(&ops,0,sizeof(ops));
 ops.mode=MTD_OOB_RAW; ops.len=sizeof(original); ops.ooblen=sizeof(oob);
 ops.datbuf=original; ops.oobbuf=oob;
 ret=mtd->read_oob(mtd,0x1fee0000,&ops);
 if(ret || ops.retlen!=sizeof(original) || ops.oobretlen!=sizeof(oob))
  return CMD_RET_FAILURE;
 if(oob[0]!=0xff || oob[1]!=0xff) return CMD_RET_FAILURE;
 for(step=0;step<4;step++) {
  n->ecc.calculate(mtd,original+step*512,parity);
  if(memcmp(parity,oob+12+step*13,13)) {
   puts("BCH parity does not match the Linux-written page\n");
   return CMD_RET_FAILURE;
  }
  for(bits=1;bits<=8;bits++) {
   memcpy(data,original+step*512,512);
   for(i=0;i<bits;i++) {
    unsigned bit=17+i*53;
    data[bit/8]^=1U<<(bit%8);
   }
   n->ecc.calculate(mtd,data,parity);
   ret=n->ecc.correct(mtd,data,oob+12+step*13,parity);
   if(ret!=(int)bits || memcmp(data,original+step*512,512)) {
    printf("RAM BCH repair failed step%u bits%u ret%d\n",step,bits,ret);
    return CMD_RET_FAILURE;
   }
  }
 }
 puts("BCH cross-reader PASS: all 4 sectors, exact parity, 1..8 RAM bit repairs\n");
 return 0;
}
U_BOOT_CMD(u2nandcheck,1,0,do_u2nandcheck,"read scratch page and test BCH in RAM","");
