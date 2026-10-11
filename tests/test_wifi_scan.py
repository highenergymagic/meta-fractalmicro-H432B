# SPDX-License-Identifier: MIT
"""Compile the regulatory scan plan and actual SiteSurvey wire parameters."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


@unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                     "native C tests require the pinned container compiler")
class WifiScan(unittest.TestCase):
    def test_active_directed_passive_and_regulatory_partition(self):
        source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
static void put_unaligned_le32(u32 value, u8 *p)
{
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}
""" + (FILES / "h432b-wifi-scan.h").read_text() + r"""
int main(void)
{
    struct wifi_scan_plan plan;
    u8 parameters[84], ssid[33];
    memset(ssid, 'x', sizeof(ssid));
    assert(wifi_scan_plan_init(&plan, true, ssid, 33) == -EINVAL);
    assert(wifi_scan_plan_init(&plan, true, NULL, 1) == -EINVAL);
    assert(wifi_scan_plan_init(&plan, true, ssid, 32) == 0);
    assert(wifi_scan_add_channel(&plan, 0, true) == -EINVAL);
    assert(wifi_scan_add_channel(&plan, 14, true) == -EINVAL);
    for (unsigned int i = 1; i <= 13; i++)
        assert(wifi_scan_add_channel(&plan, i, i != 6) == 0);
    assert(plan.count[0] == 3 && plan.count[1] == 10);
    assert(wifi_scan_add_channel(&plan, 1, true) == -EINVAL);
    assert(wifi_scan_parameters(&plan, 2, parameters) == -EINVAL);
    assert(wifi_scan_parameters(&plan, 0, parameters) == 0);
    assert(parameters[0] == 0 && parameters[4] == 48 && parameters[8] == 0);
    assert(parameters[50] == 1 && parameters[51] == 6 && parameters[52] == 12);
    assert(parameters[53] == 13 && parameters[83] == 3);
    for (unsigned int i = 12; i < 50; i++) assert(parameters[i] == 0);
    assert(wifi_scan_parameters(&plan, 1, parameters) == 0);
    assert(parameters[0] == 1 && parameters[8] == 32 && parameters[83] == 10);
    assert(memcmp(parameters + 12, ssid, 32) == 0);
    assert(parameters[44] == 0 && parameters[45] == 0 && parameters[46] == 0);
    for (unsigned int i = 0; i < parameters[83]; i++) {
        assert(parameters[51 + i] <= 11);
        assert(parameters[51 + i] != 6);
    }
    assert(wifi_scan_plan_init(&plan, false, NULL, 0) == 0);
    for (unsigned int i = 1; i <= 13; i++)
        assert(wifi_scan_add_channel(&plan, i, true) == 0);
    assert(plan.count[0] == 13 && plan.count[1] == 0);
    assert(wifi_scan_parameters(&plan, 1, parameters) == -EINVAL);
    assert(wifi_scan_parameters(&plan, 0, parameters) == 0 && parameters[0] == 0);
    assert(wifi_scan_plan_init(&plan, true, NULL, 0) == 0);
    assert(wifi_scan_add_channel(&plan, 1, true) == 0);
    assert(wifi_scan_parameters(&plan, 1, parameters) == 0);
    assert(parameters[0] == 1 && parameters[8] == 0 && parameters[83] == 1);
    wifi_scan_restrict(parameters, 0x1fff, 0);
    assert(parameters[83] == 0);
    assert(wifi_scan_plan_init(&plan, true, NULL, 0) == 0);
    for (unsigned int i = 1; i <= 13; i++)
        assert(wifi_scan_add_channel(&plan, i, true) == 0);
    assert(wifi_scan_parameters(&plan, 1, parameters) == 0);
    wifi_scan_restrict(parameters, BIT(0) | BIT(5), BIT(5));
    assert(parameters[83] == 1 && parameters[51] == 6);
    for (unsigned int i = 52; i < 83; i++) assert(parameters[i] == 0);
    assert(wifi_scan_parameters(&plan, 0, parameters) == 0);
    wifi_scan_restrict(parameters, BIT(12), 0);
    assert(parameters[83] == 1 && parameters[51] == 13);
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="wifi-scan-") as directory:
            path = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            path.write_text(source)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
