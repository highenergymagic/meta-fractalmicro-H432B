# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"
FILES = ROOT / "files"

class NANDTiming(unittest.TestCase):
    def test_phase_counters_and_bounded_report(self):
        compiler = os.environ.get("BOOTSTATE_NATIVE_CC")
        if not compiler:
            self.skipTest("Pinned native compiler not configured")
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / "common.h").write_text(
                "#include <stdio.h>\n#include <string.h>\n"
                "typedef unsigned long long u64;\n"
                "static unsigned long get_tbclk(void) { return 1000000; }\n")
            (work / "div64.h").write_text("#define do_div(n,b) ((n) /= (b))\n")
            (work / "test.c").write_text(r'''
#include <assert.h>
#include <string.h>
#include <nand-timing.h>
int main(void) {
 char buf[256], shortbuf[8];
 assert(!h432b_nand_timing_active());
 h432b_nand_timing_add(H432B_FIFO, 999000, 999);
 h432b_nand_timing_reset();
 assert(h432b_nand_timing_active());
 h432b_nand_timing_add(H432B_FIFO, 1234000, 2048);
 h432b_nand_timing_add(H432B_ENCODE, 250000, 512);
 h432b_nand_timing_add(H432B_METRICS, 900000, 99);
 h432b_nand_timing_kernel();
 h432b_nand_timing_add(H432B_COMMAND, 5000, 0);
 h432b_nand_timing_add(H432B_CRC, 1500000, 8192);
 assert(h432b_nand_timing_format(buf, sizeof(buf)) > 0);
 assert(!strcmp(buf, " openh432.nand_scan=2048,1234,0,250,0,0,0"
                     " openh432.nand_load=0,0,5,0,0,8192,1500"));
 assert(!h432b_nand_timing_active());
 h432b_nand_timing_reset();
 assert(h432b_nand_timing_format(shortbuf, sizeof(shortbuf)) == -1);
 return 0;
}
''')
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(work), "-I" + str(FILES / "nand"),
                            str(FILES / "nand/u2nandtiming.c"), str(work / "test.c"),
                            "-o", str(work / "test")], check=True)
            subprocess.run([str(work / "test")], check=True)

    def test_instrumentation_preserves_operations(self):
        source = (FILES / "nand/u2nand.c").read_text()
        for operation in ("timed_calculate(m, data, ecc)",
                          "timed_correct(m, data, read_ecc, calc_ecc)",
                          "timed_command(m, command, column, page)",
                          "u2_nand_read_fifo(buf, len)"):
            self.assertIn(operation, source)
        crc = (FILES / "nand/crc-timing.patch").read_text()
        self.assertIn("!h432b_nand_timing_active()", crc)
        self.assertIn("!defined(USE_HOSTCC)", crc)
        handoff = (FILES / "bootstate/u2bootstate.c").read_text()
        self.assertIn("sizeof(commandline) - count", handoff)
        self.assertIn("h432b_nand_timing_kernel()", handoff)
