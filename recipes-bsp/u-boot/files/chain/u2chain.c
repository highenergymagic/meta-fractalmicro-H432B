// SPDX-License-Identifier: GPL-2.0-or-later
/* Fixed-address CE boot strap. Embed the separately built high-RAM NAND reader. */
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include "u2stage-meta.h"
extern const unsigned char u2_stage_start[],u2_stage_end[];

static int do_u2chain(cmd_tbl_t *cmdtp,int flag,int argc,char *const argv[])
{
 unsigned size=u2_stage_end-u2_stage_start;
 void *destination=(void *)0x46000000;
 if(argc!=1 || size!=U2_STAGE_BYTES || size<68 || size>0x80000 ||
    u2_stage_start[3]!=0xea || readl(u2_stage_start+0x40)!=0x46000000 ||
    crc32(0,u2_stage_start,size)!=U2_STAGE_CRC) {
  puts("NAND second stage rejected: role/length/CRC mismatch\n");
  return CMD_RET_FAILURE;
 }
 memcpy(destination,u2_stage_start,size);
 flush_cache((ulong)destination,size);
 if(crc32(0,destination,size)!=U2_STAGE_CRC) return CMD_RET_FAILURE;
 /* Use the same go handoff already qualified by RAM staging from NAND51. */
 return run_command("go 46000000",0);
}
U_BOOT_CMD(u2chain,1,0,do_u2chain,"launch the embedded verified NAND boot stage","");
