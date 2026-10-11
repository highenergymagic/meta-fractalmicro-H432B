# SPDX-License-Identifier: MIT
"""Compile the actual polled-seek helpers against a deterministic bus model."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


def added_function(patch, name):
    start = re.search(r"^\+static int " + name + r"\(", patch, re.M)
    if not start:
        raise AssertionError(f"missing function {name}")
    lines = patch[start.start():].splitlines()
    result = []
    for line in lines:
        if not line.startswith("+"):
            raise AssertionError(f"incomplete added function {name}")
        result.append(line[1:])
        if line == "+}":
            return "\n".join(result)
    raise AssertionError(f"unterminated function {name}")


class FmSeek(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "pinned native C compiler required")
    def test_actual_polled_seek_cleanup_and_error_paths(self):
        patch = (FILES / "0018-h432b-fm-radio.patch").read_text()
        helpers = "\n".join(added_function(patch, name) for name in
                            ("si470x_poll_stc", "si470x_seek_polled"))
        prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint16_t u16;
#define ARRAY_SIZE(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define msecs_to_jiffies(v) (v)
#define time_before(a, b) ((a) < (b))
#define dev_err_ratelimited(...) ((void)0)
#define V4L2_TUNER_RADIO 1
#define POWERCFG 2
#define CHANNEL 3
#define SYSCONFIG2 5
#define STATUSRSSI 10
#define READCHAN 11
#define POWERCFG_SEEK 0x100
#define POWERCFG_SKMODE 0x400
#define POWERCFG_SEEKUP 0x200
#define CHANNEL_TUNE 0x8000
#define CHANNEL_CHAN 0x3ff
#define READCHAN_READCHAN 0x3ff
#define SYSCONFIG2_SPACE 0x30
#define STATUSRSSI_STC 0x4000
#define STATUSRSSI_SF 0x2000
struct v4l2_hw_freq_seek {
    unsigned int type, rangelow, rangehigh, spacing, wrap_around, seek_upward;
};
struct si470x_device {
    u16 registers[16];
    int band;
    unsigned int spacing;
    int (*get_register)(struct si470x_device *, int);
    int (*set_register)(struct si470x_device *, int);
};
static const struct { unsigned int rangelow, rangehigh; } bands[] = {
    {87500, 108000}, {76000, 108000}, {76000, 90000}
};
static unsigned long jiffies;
static unsigned int tune_timeout = 60, seek_timeout = 100;
static unsigned int frequency;
static int mode, writes, retunes;
static bool started;
static void msleep(unsigned int delay) { jiffies += delay; assert(jiffies < 500); }
static int get_register(struct si470x_device *r, int reg)
{
    if (reg == STATUSRSSI) {
        if (mode == 3 && (r->registers[POWERCFG] & POWERCFG_SEEK))
            return -EIO;
        r->registers[STATUSRSSI] = 0;
        if ((r->registers[POWERCFG] & POWERCFG_SEEK) && mode != 2)
            r->registers[STATUSRSSI] = STATUSRSSI_STC | (mode == 1 ? STATUSRSSI_SF : 0);
    } else if (reg == READCHAN) {
        r->registers[READCHAN] = 123;
    }
    return 0;
}
static int set_register(struct si470x_device *r, int reg)
{
    writes++;
    if (reg == POWERCFG && (r->registers[POWERCFG] & POWERCFG_SEEK))
        started = true;
    if (mode == 4 && started && reg == POWERCFG && !(r->registers[POWERCFG] & POWERCFG_SEEK)) {
        mode = 0;
        return -EIO;
    }
    return 0;
}
static int si470x_get_freq(struct si470x_device *r, unsigned int *value)
{
    (void)r; *value = frequency; return 0;
}
static int si470x_set_freq(struct si470x_device *r, unsigned int value)
{
    (void)r; frequency = value; retunes++; return 0;
}
static int si470x_set_band(struct si470x_device *r, int band)
{
    r->band = band;
    r->registers[SYSCONFIG2] = (r->registers[SYSCONFIG2] & ~0xc0) | (band << 6);
    return 0;
}
'''
        suffix = r'''
int main(void)
{
    struct v4l2_hw_freq_seek seek = {
        .type = V4L2_TUNER_RADIO, .rangelow = 76000, .rangehigh = 90000,
        .spacing = 200000, .seek_upward = 1
    };
    for (int test = 0; test < 5; test++) {
        struct si470x_device r = {.get_register = get_register, .set_register = set_register};
        int ret;
        mode = test; writes = retunes = 0; started = false; jiffies = 0;
        frequency = 101000; r.spacing = 1; r.registers[SYSCONFIG2] = 0x1017;
        ret = si470x_seek_polled(&r, &seek);
        assert(started);
        assert(!(r.registers[POWERCFG] & POWERCFG_SEEK));
        if (test == 0) {
            assert(ret == 0 && r.band == 2 && r.spacing == 0);
            assert((r.registers[CHANNEL] & CHANNEL_CHAN) == 123);
            assert(frequency == 90000 && retunes == 1);
        } else {
            assert(ret == (test == 1 ? -ENODATA : test == 2 ? -ETIMEDOUT : -EIO));
            assert(frequency == 101000 && retunes == 2 && r.band == 0);
            assert(r.registers[SYSCONFIG2] == 0x1017 && r.spacing == 1);
        }
    }
    struct si470x_device r = {.get_register = get_register, .set_register = set_register};
    writes = 0; seek.rangehigh = 88000;
    assert(si470x_seek_polled(&r, &seek) == -EINVAL && writes == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "seek.c"
            binary = Path(tmp) / "seek"
            src.write_text(prefix + helpers + suffix)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=gnu11",
                            "-Wall", "-Wextra", "-Werror", str(src), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
