/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * H432B internal-display backend. Key conventions follow BRLTTY's HIMS driver.
 * Uses the board kernel ABI, not the stock firmware's external-display protocol.
 */
#include "prologue.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "log.h"
#include "brl_driver.h"
#include "../HIMS/brldefs-hm.h"
#include "h432b-keys.h"

BEGIN_KEY_NAME_TABLE(navigation)
  KEY_NAME_ENTRY(HM_KEY_Dot1, "Dot1"),
  KEY_NAME_ENTRY(HM_KEY_Dot2, "Dot2"),
  KEY_NAME_ENTRY(HM_KEY_Dot3, "Dot3"),
  KEY_NAME_ENTRY(HM_KEY_Dot4, "Dot4"),
  KEY_NAME_ENTRY(HM_KEY_Dot5, "Dot5"),
  KEY_NAME_ENTRY(HM_KEY_Dot6, "Dot6"),
  KEY_NAME_ENTRY(HM_KEY_Dot7, "Dot7"),
  KEY_NAME_ENTRY(HM_KEY_Dot8, "Dot8"),
  KEY_NAME_ENTRY(HM_KEY_Space, "Space"),
  KEY_NAME_ENTRY(HM_KEY_F1, "F1"),
  KEY_NAME_ENTRY(HM_KEY_F2, "F2"),
  KEY_NAME_ENTRY(HM_KEY_F3, "F3"),
  KEY_NAME_ENTRY(HM_KEY_F4, "F4"),
  KEY_NAME_ENTRY(HM_KEY_BS_LeftScrollUp, "LeftScrollUp"),
  KEY_NAME_ENTRY(HM_KEY_BS_LeftScrollDown, "LeftScrollDown"),
  KEY_NAME_ENTRY(HM_KEY_BS_RightScrollUp, "RightScrollUp"),
  KEY_NAME_ENTRY(HM_KEY_BS_RightScrollDown, "RightScrollDown"),
  KEY_NAME_ENTRY(32, "Media1"),
  KEY_NAME_ENTRY(33, "Media2"),
  KEY_NAME_ENTRY(34, "Media3"),
  KEY_NAME_ENTRY(35, "Media4"),
  KEY_NAME_ENTRY(36, "Media5"),
END_KEY_NAME_TABLE
BEGIN_KEY_NAME_TABLE(routing)
  KEY_GROUP_ENTRY(HM_GRP_RoutingKeys, "RoutingKey"),
END_KEY_NAME_TABLE
BEGIN_KEY_NAME_TABLES(all)
  KEY_NAME_TABLE(navigation),
  KEY_NAME_TABLE(routing),
END_KEY_NAME_TABLES
DEFINE_KEY_TABLE(all)
BEGIN_KEY_TABLE_LIST
  &KEY_TABLE_DEFINITION(all),
END_KEY_TABLE_LIST

struct InputState {
  int fd;
  unsigned char reported[KEY_CNT], pending[KEY_CNT];
  int dropped;
};
struct BrailleDataStruct {
  int display;
  struct InputState inputs[2];
  unsigned char cells[32];
  int valid;
};
static int openInput(const char *name) {
  glob_t paths;
  int found = -1;
  if (glob("/dev/input/event*", 0, NULL, &paths)) return -1;
  for (size_t i = 0; i < paths.gl_pathc; i++) {
    char actual[128] = {0};
    struct input_id id;
    int fd = open(paths.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    if (ioctl(fd, EVIOCGNAME(sizeof(actual)-1), actual) < 0 ||
        strcmp(actual, name) || ioctl(fd, EVIOCGID, &id) < 0 ||
        id.bustype != BUS_HOST) {
      close(fd);
      continue;
    }
    if (found >= 0) {
      close(fd);
      close(found);
      found = -1;
      errno = EEXIST;
      break;
    }
    found = fd;
  }
  globfree(&paths);
  if (found >= 0 && ioctl(found, EVIOCGRAB, 1) < 0) {
    close(found);
    return -1;
  }
  return found;
}
static int snapshot(struct InputState *input) {
  unsigned char bits[(KEY_CNT + 7) / 8] = {0};
  if (ioctl(input->fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return 0;
  for (unsigned int code = 0; code < KEY_CNT; code++)
    input->pending[code] = !!(bits[code / 8] & (1U << (code % 8)));
  return 1;
}
static int report(BrailleDisplay *brl, unsigned int index) {
  struct InputState *input = &brl->data->inputs[index];
  /* Report presses before releases in a frame, retaining multi-key chords. */
  for (int pressed = 1; pressed >= 0; pressed--) {
    for (unsigned int code = 0; code < KEY_CNT; code++) {
      int key = h432_key_number(index, code);
      if (key < 0 || input->reported[code] == input->pending[code] ||
          input->pending[code] != pressed) continue;
      if (!enqueueKeyEvent(brl, index ? HM_GRP_RoutingKeys : HM_GRP_NavigationKeys,
                           key, pressed)) return 0;
      input->reported[code] = pressed;
    }
  }
  return 1;
}
static void closeDevices(BrailleDisplay *brl) {
  for (unsigned int i = 0; i < 2; i++) {
    if (brl->data->inputs[i].fd >= 0) close(brl->data->inputs[i].fd);
  }
  if (brl->data->display >= 0) close(brl->data->display);
}
static int brl_construct(BrailleDisplay *brl, char **parameters, const char *device) {
  const char *names[] = {"H432B keyboard and controls", "H432B braille routing"};
  (void)parameters;
  (void)device;
  brl->data = calloc(1, sizeof(*brl->data));
  if (!brl->data) return 0;
  brl->data->display = -1;
  for (unsigned int i = 0; i < 2; i++) brl->data->inputs[i].fd = -1;
  brl->data->display = open("/dev/h432b-braille", O_WRONLY | O_CLOEXEC);
  if (brl->data->display < 0) goto fail;
  brl->textColumns = 32;
  brl->textRows = 1;
  setBrailleKeyTable(brl, &KEY_TABLE_DEFINITION(all));
  makeOutputTable(dotsTable_ISO11548_1);
  for (unsigned int i = 0; i < 2; i++) {
    brl->data->inputs[i].fd = openInput(names[i]);
    if (brl->data->inputs[i].fd < 0 ||
        !snapshot(&brl->data->inputs[i]) || !report(brl, i)) goto fail;
  }
  return 1;
fail:
  logSystemError("H432B internal braille initialization");
  closeDevices(brl);
  free(brl->data);
  brl->data = NULL;
  return 0;
}
static void brl_destruct(BrailleDisplay *brl) {
  closeDevices(brl);
  free(brl->data);
}
static int brl_writeWindow(BrailleDisplay *brl, const wchar_t *text) {
  ssize_t result;
  (void)text;
  if (brl->data->valid && !memcmp(brl->buffer, brl->data->cells, 32)) return 1;
  do {
    result = write(brl->data->display, brl->buffer, 32);
  } while (result < 0 && errno == EINTR);
  if (result != 32) return 0;
  memcpy(brl->data->cells, brl->buffer, 32);
  brl->data->valid = 1;
  return 1;
}
static int brl_readCommand(BrailleDisplay *brl, KeyTableCommandContext context) {
  (void)context;
  for (unsigned int i = 0; i < 2; i++) {
    struct InputState *input = &brl->data->inputs[i];
    for (unsigned int budget = 0; budget < 256; budget++) {
      struct input_event event;
      ssize_t size = read(input->fd, &event, sizeof(event));
      if (size < 0 && errno == EINTR) continue;
      if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      if (size != sizeof(event)) return BRL_CMD_RESTARTBRL;
      if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        input->dropped = 1;
      } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
        if (input->dropped && !snapshot(input)) return BRL_CMD_RESTARTBRL;
        input->dropped = 0;
        if (!report(brl, i)) return BRL_CMD_RESTARTBRL;
      } else if (!input->dropped && event.type == EV_KEY &&
                 event.code < KEY_CNT && event.value != 2) {
        input->pending[event.code] = !!event.value;
      }
    }
  }
  return EOF;
}
