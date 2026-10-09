# SPDX-License-Identifier: MIT
"""Board capture/jack source contracts; acoustic checks are hardware tests."""
from pathlib import Path
import unittest
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class AudioCapture(unittest.TestCase):
    def apply_audio_stack(self, directory):
        patch = (FILES / "0007-hims-u2-audio.patch").read_text()
        section = patch.split("+++ b/sound/soc/samsung/hims-u2-audio.c\n", 1)[1]
        body = "\n".join(line[1:] for line in section.splitlines()
                         if line.startswith("+")) + "\n"
        source = Path(directory) / "sound/soc/samsung/hims-u2-audio.c"
        source.parent.mkdir(parents=True)
        source.write_text(body)
        for name in ("0026-h432b-audio-dma-request-clock.patch",
                     "0027-h432b-audio-capture.patch",
                     "0028-h432b-audio-jacks.patch"):
            subprocess.run(["patch", "--silent", "--fuzz=0", "-p1",
                            "-i", str(FILES / name)],
                           cwd=directory, check=True)
        return source.read_text()

    def test_patch_stack_applies_without_fuzz(self):
        with tempfile.TemporaryDirectory() as directory:
            text = self.apply_audio_stack(directory)
            self.assertIn("u2->link.symmetric_rate = 1;", text)
            self.assertNotIn("u2->link.playback_only = 1;", text)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                         "native C compiler not configured")
    def test_headphone_gate_does_not_activate_idle_output(self):
        with tempfile.TemporaryDirectory() as directory:
            text = self.apply_audio_stack(directory)
            code = text[text.index("static void u2_mute"):
                        text.index("static void u2_unregister_headphone")]
            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define SND_JACK_HEADPHONE 1
#define NOTIFY_OK 1
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
struct mutex { int held; };
struct gpio_desc { int value; };
struct notifier_block { int unused; };
struct u2_audio {
    struct mutex output_lock;
    bool output_active;
    struct gpio_desc *unmute, *speaker;
    struct notifier_block headphone_notifier;
};
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
static void gpiod_set_value_cansleep(struct gpio_desc *g, int v) { g->value = v; }
"""
            checks = r"""
int main(void) {
    struct gpio_desc unmute = {0}, speaker = {0};
    struct u2_audio u = { .unmute = &unmute, .speaker = &speaker };
    u2_headphone_changed(&u.headphone_notifier, 0, NULL);
    assert(!speaker.value && !unmute.value);
    u.output_active = true; unmute.value = 1; speaker.value = 1;
    u2_headphone_changed(&u.headphone_notifier, SND_JACK_HEADPHONE, NULL);
    assert(!speaker.value && unmute.value);
    u2_headphone_changed(&u.headphone_notifier, 0, NULL);
    assert(speaker.value && unmute.value);
    u2_mute(&u);
    assert(!u.output_active && !speaker.value && !unmute.value);
    u2_headphone_changed(&u.headphone_notifier, 0, NULL);
    assert(!speaker.value && !unmute.value && !u.output_lock.held);
    return 0;
}
"""
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(harness + code + checks)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11",
                            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_capture_is_in_standard_runtime(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn("0027-h432b-audio-capture.patch", recipe)
        self.assertIn("0028-h432b-audio-jacks.patch", recipe)
        patch = (FILES / "0027-h432b-audio-capture.patch").read_text()
        self.assertIn("-\tu2->link.playback_only = 1;", patch)
        self.assertIn("+\tu2->link.symmetric_rate = 1;", patch)
        self.assertIn('{ "Internal Mic", NULL, "Mic Bias" }', patch)
        self.assertNotIn('+\t\t\t   U2_ANALOG_VOLUME_MAX', patch)

    def test_jack_gpio_polarity_and_no_wake(self):
        patch = (FILES / "0028-h432b-audio-jacks.patch").read_text()
        dt = (FILES / "s5pv210-hims-u2-audio-input.dtsi").read_text()
        self.assertIn("headphone-detect-gpios = <&gph3 4 GPIO_ACTIVE_LOW>", dt)
        self.assertIn("microphone-detect-gpios = <&gph3 0 GPIO_ACTIVE_LOW>", dt)
        self.assertNotIn("wakeup-source", dt)
        self.assertIn("samsung,pin-pud = <S5PV210_PIN_PULL_NONE>", dt)
        self.assertIn("pinctrl-0 = <&audio_jack_pins>", dt)
        self.assertNotIn(".wake = true", patch)
        self.assertNotIn("enable_irq_wake", patch)
        self.assertIn('u2->headphone_gpio.debounce_time = 50', patch)
        self.assertIn('u2->microphone_gpio.debounce_time = 50', patch)
        self.assertIn("snd_soc_jack_notifier_unregister", patch)
        self.assertIn("mutex_lock(&u2->output_lock)", patch)
        self.assertIn("WM8983_MBVSEL, WM8983_MBVSEL", patch)
        self.assertIn("if (u2->output_active)", patch)

if __name__ == "__main__":
    unittest.main()
