# SPDX-License-Identifier: MIT
"""Audio request-clock lifetime contract; live DMA tests are separate."""
from pathlib import Path
import unittest
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class AudioClock(unittest.TestCase):
    def test_stream_owns_request_clock(self):
        patch = (FILES / "0026-h432b-audio-dma-request-clock.patch").read_text()
        self.assertIn('devm_clk_get(dev, "dma-request")', patch)
        self.assertIn("clk_prepare_enable(u2->dma_request_clk)", patch)
        self.assertIn("clk_disable_unprepare(u2->dma_request_clk)", patch)
        self.assertIn(".shutdown = u2_shutdown", patch)
        self.assertNotIn("pm_runtime_forbid", patch)
        self.assertNotIn("writel(", patch)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "native C compiler not configured")
    def test_positive_constraints_and_clock_error_paths(self):
        patch = (FILES / "0026-h432b-audio-dma-request-clock.patch").read_text()
        added = "\n".join(line[1:] for line in patch.splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        start = added.index("static int u2_startup")
        end = added.index("\n\t.startup = u2_startup", start)
        functions = added[start:end]
        harness = r"""
#include <assert.h>
struct clk { int dummy; };
struct u2_audio { struct clk *dma_request_clk; };
struct snd_soc_card { struct u2_audio *data; };
struct snd_soc_pcm_runtime { struct snd_soc_card *card; };
struct snd_pcm_substream { void *runtime; };
static struct clk clock;
static struct u2_audio audio = { &clock };
static struct snd_soc_card card = { &audio };
static struct snd_soc_pcm_runtime rtd = { &card };
static int results[3], step, references, enable_error;
#define SNDRV_PCM_HW_PARAM_RATE 0
#define SNDRV_PCM_HW_PARAM_CHANNELS 1
#define SNDRV_PCM_HW_PARAM_FORMAT 2
#define SNDRV_PCM_FMTBIT_S16_LE 1
static struct snd_soc_pcm_runtime *snd_soc_substream_to_rtd(struct snd_pcm_substream *s)
{ (void)s; return &rtd; }
static void *snd_soc_card_get_drvdata(struct snd_soc_card *c) { return c->data; }
static int snd_pcm_hw_constraint_single(void *r, int p, unsigned v)
{ (void)r; (void)p; (void)v; return results[step++]; }
static int snd_pcm_hw_constraint_mask64(void *r, int p, unsigned long long v)
{ (void)r; (void)p; (void)v; return results[step++]; }
static int clk_prepare_enable(struct clk *c)
{ assert(c == &clock); if (enable_error) return enable_error; references++; return 0; }
static void clk_disable_unprepare(struct clk *c)
{ assert(c == &clock); assert(references == 1); references--; }
"""
        checks = r"""
int main(void) {
    struct snd_pcm_substream stream = {0};
    results[0] = results[1] = 1;
    assert(u2_startup(&stream) == 0);
    assert(step == 3 && references == 1);
    u2_shutdown(&stream);
    assert(references == 0);
    for (int i = 0; i < 3; i++) {
        step = 0; results[0] = results[1] = 1; results[2] = 0;
        results[i] = -22;
        assert(u2_startup(&stream) == -22);
        assert(references == 0 && step == i + 1);
    }
    step = 0; results[0] = results[1] = results[2] = 0;
    enable_error = -5;
    assert(u2_startup(&stream) == -5 && references == 0);
    step = 0; enable_error = 0;
    assert(u2_startup(&stream) == 0 && references == 1);
    u2_shutdown(&stream);
    assert(references == 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "audio-clock-test.c"
            binary = Path(directory) / "audio-clock-test"
            source.write_text(harness + functions + checks)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11",
                            "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_all_audio_images_use_matching_clock_binding(self):
        dt = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertIn("<&clocks CLK_PDMA0>", dt)
        self.assertIn('"dma-request"', dt)
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-base.inc").read_text()
        self.assertLess(recipe.index("0007-hims-u2-audio.patch"),
                        recipe.index("0026-h432b-audio-dma-request-clock.patch"))

if __name__ == "__main__":
    unittest.main()
