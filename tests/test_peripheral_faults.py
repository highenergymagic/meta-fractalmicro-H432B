# SPDX-License-Identifier: MIT
"""Compile actual peripheral helpers with deterministic transport failures."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / 'recipes-kernel/linux/files'


def function(source, name):
    found = re.search(r'^static (?:int|void|u8) ' + name + r'\([^;]+?\n\{', source, re.M)
    if not found:
        raise AssertionError(name)
    end = source.index('\n}', found.end()) + 2
    return source[found.start():end]


@unittest.skipUnless(os.environ.get('WIFI_RX_NATIVE_CC'), 'pinned native compiler required')
class PeripheralFaults(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as temp:
            c = Path(temp) / 'faults.c'
            binary = Path(temp) / 'faults'
            c.write_text(source)
            subprocess.run([os.environ['WIFI_RX_NATIVE_CC'], '-std=gnu11',
                            '-Wall', '-Wextra', '-Werror', str(c), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_battery_gpio_errors_never_become_data_bits(self):
        source = (FILES / 'h432b-battery-inventory.c').read_text()
        helpers = '\n'.join(function(source, name) for name in
                            ('battery_reset', 'battery_read_byte', 'battery_crc',
                             'battery_read_rom', 'battery_read_capacity', 'battery_read_window'))
        self.compile_run(r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#define BIT(n) (1U << (n))
#define udelay(x) ((void)(x))
#define usleep_range(x,y) ((void)(x), (void)(y))
#define raw_spin_lock_irqsave(lock, flags) ((void)(lock), (flags)=0, depth++)
#define raw_spin_unlock_irqrestore(lock, flags) ((void)(lock), (void)(flags), depth--)
struct h432b_battery_inventory { int slot, *rx, *pull_low; };
static int levels[32], cursor, count, depth, output;
static int gpiod_get_value(int *gpio) { (void)gpio; assert(cursor < count); return levels[cursor++]; }
static void gpiod_set_value(int *gpio, int value) { (void)gpio; output = value; }
static void battery_write_byte(struct h432b_battery_inventory *b, u8 data) { (void)b; (void)data; }
''' + helpers + r'''
int main(void)
{
    struct h432b_battery_inventory b = {0};
    const int reset_levels[][3] = {
        {-EIO, 0, 0}, {0, 0, 0}, {1, -EIO, 0}, {1, 1, 0},
        {1, 0, 0}, {1, 0, -EIO}, {1, 0, 1}
    };
    const int expected[] = {-EIO, -EBUSY, -EIO, -ENODEV, -EIO, -EIO, 0};
    for (unsigned int i = 0; i < sizeof(expected)/sizeof(expected[0]); i++) {
        for (int j=0; j<3; j++) levels[j] = reset_levels[i][j];
        cursor=0; count=3; depth=output=0;
        assert(battery_reset(&b) == expected[i]);
        assert(depth == 0 && output == 0);
    }
    cursor=0; count=8; depth=output=0;
    for (int j=0; j<8; j++) levels[j] = (0xa5 >> j) & 1;
    assert(battery_read_byte(&b) == 0xa5 && depth == 0 && output == 0);
    for (int fail=0; fail<8; fail++) {
        cursor=0;
        for (int j=0; j<8; j++) levels[j] = j == fail ? -EIO : 0;
        assert(battery_read_byte(&b) == -EIO && depth == 0 && output == 0);
    }
    for (int operation=0; operation<3; operation++) {
        u8 data[8] = {0};
        levels[0]=1; levels[1]=0; levels[2]=1; levels[3]=-EIO;
        cursor=0; count=4;
        int ret = operation == 0 ? battery_read_rom(&b, data) :
                  operation == 1 ? battery_read_capacity(&b, data, data) :
                                   battery_read_window(&b, data, 8, data, 8);
        assert(ret == -EIO && depth == 0 && output == 0);
    }
    return 0;
}
''')

    def test_sensor_supply_reference_balances_failures(self):
        source = (FILES / 'ami603.c').read_text()
        helpers = '\n'.join(function(source, name) for name in
                            ('ami603_disable', 'ami603_power_off', 'ami603_power_on'))
        self.compile_run(r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
#define AMI603_WIA 0xba
#define AMI603_ID 0x45
#define usleep_range(x,y) ((void)(x), (void)(y))
#define dev_err(...) ((void)0)
struct ami603 { void *vdd; bool powered; };
static int enable_error, disable_error, read_error, references;
static int regulator_enable(void *vdd) { (void)vdd; if (!enable_error) references++; return enable_error; }
static int regulator_disable(void *vdd) { (void)vdd; if (!disable_error) references--; return disable_error; }
static int ami603_read(struct ami603 *s, int reg, u8 *id, int len) {
    (void)s; (void)reg; (void)len; *id=AMI603_ID; return read_error;
}
static int ami603_standby(struct ami603 *s) { (void)s; return 0; }
''' + helpers + r'''
int main(void)
{
    struct ami603 s = {0};
    enable_error=-EIO;
    assert(ami603_power_on(&s)==-EIO && !s.powered && !references);
    enable_error=0; read_error=-EIO;
    assert(ami603_power_on(&s)==-EIO && !s.powered && !references);
    read_error=0;
    assert(ami603_power_on(&s)==0 && s.powered && references==1);
    assert(ami603_power_on(&s)==0 && references==1);
    disable_error=-EIO;
    assert(ami603_disable(&s)==-EIO && s.powered && references==1);
    disable_error=0;
    ami603_power_off(&s);
    assert(!s.powered && !references);
    ami603_power_off(&s);
    assert(!references);
    return 0;
}
''')
