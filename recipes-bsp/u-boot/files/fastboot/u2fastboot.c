// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * H432B fastboot 0.4 RAM transport, Fractal Microsystems, 2026.
 * Implements the Android fastboot wire protocol on the existing S3C gadget.
 * No storage commands or arbitrary-address transfer commands are implemented.
 */
#include <common.h>
#include <asm/errno.h>
#include <linux/usb/ch9.h>
#include <usbdescriptors.h>
#include <linux/usb/gadget.h>
#include <libfdt.h>
#include <version.h>
#include "u2fastboot.h"

#define RX_SIZE 16384U
static struct usb_ep *in_ep, *out_ep;
static struct usb_request *in_req, *out_req;
static unsigned char rx[RX_SIZE] __attribute__((aligned(64)));
static char tx[64] __attribute__((aligned(64)));
static int online, rx_ready, tx_done, tx_busy, fatal, action;
static unsigned wanted, received, valid, image_crc;
static struct u2_fb_image image;
extern int u2_fastboot_linux(const struct u2_fb_image *image);

static struct usb_endpoint_descriptor in_desc = {
 .bLength=7, .bDescriptorType=USB_DT_ENDPOINT, .bEndpointAddress=0x81,
 .bmAttributes=USB_ENDPOINT_XFER_BULK,
};
static struct usb_endpoint_descriptor out_desc = {
 .bLength=7, .bDescriptorType=USB_DT_ENDPOINT, .bEndpointAddress=0x02,
 .bmAttributes=USB_ENDPOINT_XFER_BULK,
};

static void rx_complete(struct usb_ep *ep, struct usb_request *r)
{
 if (r->status) { fatal=1; valid=0; return; }
 rx_ready=1;
}
static void tx_complete(struct usb_ep *ep, struct usb_request *r)
{
 tx_busy=0;
 if (r->status) { fatal=1; action=0; return; }
 tx_done=1;
}
static int queue_rx(void)
{
 unsigned n=wanted ? min(wanted-received,RX_SIZE) : 4096;
 out_req->buf=rx;
 out_req->length=n;
 out_req->zero=0;
 rx_ready=0;
 if (usb_ep_queue(out_ep,out_req,0)) { fatal=1; valid=0; return -1; }
 return 0;
}
static void response(const char *status, const char *message)
{
 /* At most 63 bytes, never an ambiguous exact max-packet response. */
 snprintf(tx,sizeof(tx),"%.4s%.59s",status,message);
 in_req->buf=tx;
 in_req->length=strlen(tx);
 in_req->zero=0;
 tx_busy=1;
 tx_done=0;
 if (usb_ep_queue(in_ep,in_req,0)) { tx_busy=0; fatal=1; action=0; }
}
int u2fastboot_bind(struct usb_gadget *g)
{
 struct usb_ep *ep;
 list_for_each_entry(ep,&g->ep_list,ep_list) {
  if (!strcmp(ep->name,"ep1in-bulk")) in_ep=ep;
  if (!strcmp(ep->name,"ep2out-bulk")) out_ep=ep;
 }
 if (!in_ep || !out_ep) return -ENODEV;
 in_req=usb_ep_alloc_request(in_ep,0);
 out_req=usb_ep_alloc_request(out_ep,0);
 if (!in_req || !out_req) {
  if(in_req) usb_ep_free_request(in_ep,in_req);
  if(out_req) usb_ep_free_request(out_ep,out_req);
  in_req=out_req=NULL;
  return -ENOMEM;
 }
 in_req->complete=tx_complete;
 out_req->complete=rx_complete;
 return 0;
}
void u2fastboot_disable(void)
{
 int was_online=online;
 online=0;
 if (was_online) {
  usb_ep_disable(out_ep);
  usb_ep_disable(in_ep);
 }
 wanted=received=valid=image_crc=0;
 rx_ready=tx_done=tx_busy=fatal=action=0;
}
void u2fastboot_unbind(void)
{
 u2fastboot_disable();
 if(in_req) usb_ep_free_request(in_ep,in_req);
 if(out_req) usb_ep_free_request(out_ep,out_req);
 in_req=out_req=NULL;
}
int u2fastboot_configure(struct usb_gadget *g, unsigned config)
{
 int r;
 u2fastboot_disable();
 if (!config) return 0;
 in_desc.wMaxPacketSize=out_desc.wMaxPacketSize=
  cpu_to_le16(g->speed==USB_SPEED_HIGH ? 512 : 64);
 r=usb_ep_enable(in_ep,&in_desc);
 if(r) return r;
 r=usb_ep_enable(out_ep,&out_desc);
 if(r) { usb_ep_disable(in_ep); return r; }
 online=1;
 return queue_rx();
}
extern const char *u2_identity_board_id(void);

static void getvar(const char *name)
{
 char value[32];
 const char *v=NULL;
 if (!strcmp(name,"version")) v="0.4";
 else if (!strcmp(name,"version-bootloader")) v=U_BOOT_VERSION;
 else if (!strcmp(name,"product")) v="h432b";
 else if (!strcmp(name,"serialno")) {
  v=u2_identity_board_id();
  if(!v) v="";
 }
 else if (!strcmp(name,"max-download-size")) v="0x02000000";
 else if (!strcmp(name,"is-userspace")) v="no";
 else if (!strcmp(name,"secure")) v="no";
 else if (!strcmp(name,"unlocked")) v="yes";
 else if (!strcmp(name,"has-slot:boot")) v="no";
 else if (!strcmp(name,"download-size")) {
  snprintf(value,sizeof(value),"%08x",valid); v=value;
 } else if (!strcmp(name,"download-crc32") && valid) {
  snprintf(value,sizeof(value),"%08x",image_crc); v=value;
 }
 if (v) response("OKAY",v);
 else response("FAIL","unknown variable");
}
static void command(unsigned n)
{
 unsigned i;
 if (!n || n>=4096) { response("FAIL","invalid command length"); return; }
 for(i=0;i<n;i++)
  if(rx[i]<32 || rx[i]>126) { response("FAIL","command must be ASCII"); return; }
 rx[n]=0;
 if (!strncmp((char *)rx,"getvar:",7)) getvar((char *)rx+7);
 else if (!strncmp((char *)rx,"download:",9)) {
  valid=image_crc=received=wanted=0;
  if(u2_fb_size(rx+9,n-9,&wanted)) {
   response("FAIL","download size invalid or exceeds 32MiB"); return;
  }
  /* response can use a separate local payload; never overlap snprintf. */
  { char size[9]; snprintf(size,sizeof(size),"%08x",wanted);
    response("DATA",size); }
 } else if (!strcmp((char *)rx,"boot")) {
  const unsigned char *p=(const unsigned char *)U2_FB_ADDRESS;
  if (!valid || crc32(0,p,valid)!=image_crc ||
      u2_fb_layout(p,valid,&image) ||
      u2_fb_le32(p+image.kernel+36)!=0x016f2818 ||
      fdt_check_header(p+image.dtb) ||
      fdt_totalsize(p+image.dtb)>image.dtb_size) {
   response("FAIL","invalid H432B Android-v2 boot image"); return;
  }
  action=1;
  response("OKAY","");
 } else if (!strcmp((char *)rx,"reboot")) {
  action=2; response("OKAY","");
 } else if (!strncmp((char *)rx,"flash:",6) ||
            !strncmp((char *)rx,"erase:",6)) {
  response("FAIL","storage backend not qualified; RAM only");
 } else response("FAIL","unsupported command");
}
void u2fastboot_poll(void)
{
 unsigned n;
 if (!online || fatal || tx_busy) return;
 if (tx_done) {
  tx_done=0;
  /* Execute only after the host has received the final OKAY. */
  if (action) {
   int a=action;
   action=0;
   if (a==2) run_command("reset",0);
   else u2_fastboot_linux(&image);
   /* A returning boot is failure; never claim another successful transfer. */
   fatal=1;
   return;
  }
  queue_rx();
  return;
 }
 if (!rx_ready) return;
 rx_ready=0;
 n=out_req->actual;
 if (wanted) {
  if (received>wanted || n>wanted-received || n>RX_SIZE) {
   wanted=valid=0;
   response("FAIL","download overflow"); return;
  }
  /* Zero-length packets are ignored, short packets continue the transfer. */
  memcpy((void *)(U2_FB_ADDRESS+received),rx,n);
  received+=n;
  if (received==wanted) {
   valid=wanted; wanted=0;
   image_crc=crc32(0,(void *)U2_FB_ADDRESS,valid);
   response("OKAY","");
  } else queue_rx();
 } else command(n);
}
