/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "bootmode.h"
struct mock { unsigned value, writes; int fail; };
static unsigned get(void *p) { return ((struct mock *)p)->value; }
static void set(void *p, unsigned value) {
    struct mock *m = p;
    m->writes++;
    if (!m->fail) m->value = value;
}
int main(void) {
    unsigned unknown[] = {0, 0xffffffffU, 0x48345254U,
                          H432B_MODE_FASTBOOT ^ 1U, 0x12345678U};
    unsigned i;
    struct mock m;
    for (i = 0; i < sizeof(unknown)/sizeof(unknown[0]); i++) {
        m = (struct mock){unknown[i], 0, 0};
        assert(h432b_take_bootmode(get, set, &m) == 0);
        assert(m.writes == 0 && m.value == unknown[i]);
    }
    m = (struct mock){H432B_MODE_FASTBOOT, 0, 0};
    assert(h432b_take_bootmode(get, set, &m) == 1);
    assert(m.writes == 1 && m.value == 0);
    assert(h432b_take_bootmode(get, set, &m) == 0 && m.writes == 1);
    m = (struct mock){H432B_MODE_NORMAL, 0, 0};
    assert(h432b_take_bootmode(get, set, &m) == 0);
    assert(m.writes == 1 && m.value == 0);
    m = (struct mock){H432B_MODE_FASTBOOT, 0, 1};
    assert(h432b_take_bootmode(get, set, &m) == -1);
    assert(m.writes == 1 && m.value == H432B_MODE_FASTBOOT);
    return 0;
}
