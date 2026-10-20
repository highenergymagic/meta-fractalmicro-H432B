# SPDX-License-Identifier: MIT
"""Compile the actual divider functions against a deterministic MMIO model."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
SOURCE = FILES / "s5pv210-fixed-cpufreq.c"


def function(text, name):
    start = text.index("static ", text.index(name) - 30)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class FixedCpufreqTests(unittest.TestCase):
    def test_real_transition_and_rollback(self):
        text = SOURCE.read_text()
        definitions = "\n".join(re.findall(r"^#define .*$", text, re.M))
        functions = "\n".join(function(text, name) for name in (
            "clock_configuration_valid(void)", "change_dividers(u32 fields)",
            "fixed_target(struct", "restore_firmware_clock(struct",
            "fixed_resume(struct"))
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) >> (31 - (h))) & ((~0U) << (l)))
static u32 registers[0x6200 / 4];
static unsigned char *base = (unsigned char *)registers;
static u32 fixed_dividers;
static bool failed;
static bool blocked;
static int writes, fail_polls, polls;
static int transition_lock, notifications;
struct cpufreq_policy { unsigned int cur, max; };
struct cpufreq_freqs { unsigned int old, new; };
static struct { u32 driver_data; } frequencies[] = {{0x101}, {0x300}};
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
static void cpufreq_freq_transition_begin(struct cpufreq_policy *p,
                                         struct cpufreq_freqs *f) {
    assert(f->old == p->cur);
    notifications++;
}
static void cpufreq_freq_transition_end(struct cpufreq_policy *p,
                                       struct cpufreq_freqs *f, int error) {
    if (!error)
        p->cur = f->new;
    notifications++;
}
#define dev_err(...) ((void)0)
#define dev_crit(...) ((void)0)
#define local_irq_save(f) ((f) = 0)
#define local_irq_restore(f) ((void)(f))
static u32 readl(void *p) { return *(u32 *)p; }
static void writel(u32 v, void *p) {
    assert(p == base + 0x300);
    writes++;
    *(u32 *)p = v;
}
#define readl_poll_timeout_atomic(a, v, cond, delay, timeout) \
    (polls++, (v) = readl(a), (void)(cond), assert((delay) == 1 && (timeout) == 500), \
     fail_polls-- > 0 ? -ETIMEDOUT : 0)
"""
        cases = r"""
static void reset(void) {
    registers[APLL_CON / 4] = APLL_800;
    registers[CLK_SRC0 / 4] = 0x10000111;
    registers[CLK_DIV0 / 4] = 0x14131330;
    registers[ARM_MCS_CON / 4] = 1;
    fixed_dividers = 0x14131330 & ~DIV_MASK;
    failed = false;
    blocked = false;
    notifications = 0;
    writes = polls = fail_polls = 0;
}
int main(void) {
    struct cpufreq_policy policy = {.cur = 400000, .max = 400000};
    reset();
    assert(clock_configuration_valid());
    assert(change_dividers(DIV_400) == 0);
    assert(registers[CLK_DIV0 / 4] == 0x14131131);
    assert(800 / ((DIV_400 & ARM_DIV) + 1) /
           (((DIV_400 & HCLK_DIV) >> 8) + 1) == 200);
    assert(change_dividers(DIV_800) == 0);
    assert(registers[CLK_DIV0 / 4] == 0x14131330);
    assert(writes == 2 && polls == 2);
    assert(change_dividers(DIV_800) == 0 && writes == 2);
    reset();
    fail_polls = 1;
    assert(change_dividers(DIV_400) == -ETIMEDOUT);
    assert(registers[CLK_DIV0 / 4] == 0x14131330);
    assert(failed && writes == 2 && polls == 2);
    assert(change_dividers(DIV_400) == -EIO && writes == 2);
    reset();
    fail_polls = 2;
    assert(change_dividers(DIV_400) == -ETIMEDOUT);
    assert(failed && polls == 2); /* Rollback also terminates. */
    reset();
    registers[APLL_CON / 4] ^= BIT(16);
    assert(change_dividers(DIV_400) == -EIO && writes == 0);
    reset();
    registers[CLK_SRC0 / 4] |= BIT(16);
    assert(change_dividers(DIV_400) == -EIO && writes == 0);
    reset();
    registers[ARM_MCS_CON / 4] = 3;
    assert(change_dividers(DIV_400) == -EIO && writes == 0);
    reset();
    registers[CLK_DIV0 / 4] ^= BIT(4);
    assert(change_dividers(DIV_400) == -EIO && writes == 0);
    reset();
    assert(fixed_target(&policy, 0) == 0);
    assert(restore_firmware_clock(&policy) == 0);
    assert(policy.cur == 800000 && policy.max == 400000);
    assert(blocked && notifications == 2);
    assert(fixed_target(&policy, 0) == -EBUSY);
    assert(fixed_resume(&policy) == 0 && !blocked);
    assert(fixed_target(&policy, 0) == 0);
    assert(fixed_resume(&policy) == -EIO); /* Resume expects firmware clock. */
    reset();
    assert(change_dividers(DIV_400) == 0);
    policy.cur = 400000;
    fail_polls = 1;
    assert(restore_firmware_clock(&policy) == -ETIMEDOUT);
    assert(policy.cur == 400000 && !blocked && failed);
    return 0;
}
"""
        compiler = os.environ.get("POWER_NATIVE_CC") or shutil.which("cc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            test.write_text(harness + definitions + "\n" + functions + cases)
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter",
                            str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_lifecycle_and_scope(self):
        text = SOURCE.read_text()
        self.assertIn(".suspend = restore_firmware_clock", text)
        self.assertIn(".resume = fixed_resume", text)
        reboot = function(text, "fixed_reboot(struct")
        self.assertIn("restore_firmware_clock(policy)", reboot)
        restore = function(text, "restore_firmware_clock(struct")
        self.assertIn("cpufreq_freq_transition_begin", restore)
        self.assertIn("cpufreq_freq_transition_end", restore)
        self.assertNotIn("cpufreq_driver_target", restore)  # Ignores user caps.
        self.assertIn("blocked = true", restore)
        self.assertNotIn("regulator_set", text)
        self.assertNotIn("CPUFREQ_CONST_LOOPS", text)
        self.assertEqual(set(re.findall(r"writel\([^;]+, base \+ (\w+)\)", text)),
                         {"CLK_DIV0"})
        self.assertIn("CLK_GET_RATE_NOCACHE",
                      (FILES / "0041-s5pv210-fixed-bus-cpufreq.patch").read_text())

    def test_runtime_default_and_mutual_exclusion(self):
        config = (FILES / "h432b-cpufreq.config").read_text()
        self.assertIn("CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE=y", config)
        self.assertIn("# CONFIG_ARM_S5PV210_CPUFREQ is not set", config)
        runtime = (FILES.parent / "linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertTrue(runtime.rstrip().endswith("require h432b-cpufreq.inc"))
        patch = (FILES / "0041-s5pv210-fixed-bus-cpufreq.patch").read_text()
        self.assertIn("depends on !ARM_S5PV210_FIXED_CPUFREQ", patch)


if __name__ == "__main__":
    unittest.main()
