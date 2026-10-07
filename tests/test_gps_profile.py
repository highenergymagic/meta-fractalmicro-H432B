# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
FILES=ROOT/"recipes-kernel/linux/files"

class GpsProfileTests(unittest.TestCase):
    def test_gpio_and_uart_scope(self):
        text=(FILES/"s5pv210-hims-u2-gps.dtsi").read_text()
        for expected in ("&uart1", "gpio = <&gpe1 0 GPIO_ACTIVE_HIGH>",
                         'samsung,pins = "gpa0-4"'):
            self.assertIn(expected,text)
        for forbidden in ("gpa0-6", "gpa0-7", "microvolt", "console=", "/delete-property/"):
            self.assertNotIn(forbidden,text)
        self.assertIn('samsung,pins = "gpa0-5"',text)
        self.assertNotIn("hims,gps-query-test",text)
        self.assertNotIn("regulator-always-on",text)
        self.assertIn("reset-gpios = <&gph3 3 GPIO_ACTIVE_LOW>",text)
    def test_reset_sequence(self):
        text=(FILES/"h432b-gps-power.c").read_text()
        self.assertIn('devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH)',text)
        self.assertLess(text.index("regulator_enable("),text.index("fsleep(100000)"))
        self.assertLess(text.index("fsleep(100000)"),text.index("gpiod_set_value_cansleep(gps->reset, 0)"))
        self.assertIn("devm_add_action_or_reset",text)
    def test_nand_and_diagnostic_share_gps(self):
        recipes=ROOT/"recipes-kernel/linux"
        recipe=(recipes/"linux-h432b-platform.inc").read_text()
        self.assertIn("0014-h432b-gps-power.patch",recipe)
        self.assertIn("u2-gps.config",recipe)
        self.assertNotIn("external-sd",recipe)
        tree=(FILES/"s5pv210-hims-u2-reboot-test.dts").read_text()
        self.assertIn('#include "s5pv210-hims-u2-gps.dtsi"',tree)
        diagnostic=(recipes/"linux-h432b-gps-test_6.12.111.bb").read_text()
        self.assertNotIn("0013-h432b-gps-power-test.patch",diagnostic)
        self.assertNotIn("h432b-gps-power-test.c",diagnostic)

    def test_runtime_has_no_diagnostic_permission_markers(self):
        tree=(FILES/"s5pv210-hims-u2-gps.dtsi").read_text()
        self.assertNotIn("hims,gps-query-test",tree)
        self.assertNotIn("hims,gps-receive-test",tree)
        diagnostic=(FILES/"s5pv210-hims-u2-gps-test.dts").read_text()
        self.assertIn("hims,gps-query-test",diagnostic)
        self.assertIn("hims,gps-receive-test",diagnostic)

    def test_runtime_does_not_claim_suspend_support(self):
        driver=(FILES/"h432b-gps-power.c").read_text()
        self.assertNotIn(".pm =",driver)
        self.assertIn("no runtime or system PM",driver)
        config=(FILES/"u2-gps.config").read_text()
        self.assertIn("CONFIG_H432B_GPS_POWER=y",config)
        self.assertIn("CONFIG_REGULATOR_FIXED_VOLTAGE=y",config)

    def test_runtime_storage_is_writable_but_factory_protected(self):
        tree=(FILES/"s5pv210-hims-u2-runtime.dts").read_text()
        self.assertIn("hims,write-window = <0x00400000 0x1fb00000>",tree)
        self.assertIn("&sdhci1",tree)
        self.assertIn("/delete-property/ hims,read-only-probe",tree)
        self.assertNotIn("hims,internal-sd-write-test",tree)

    def test_default_runtime_provider(self):
        machine=(ROOT/"conf/machine/h432b.conf").read_text()
        recipe=(ROOT/"recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn('PREFERRED_PROVIDER_virtual/kernel = "linux-h432b-runtime"',machine)
        self.assertIn('H432B_KERNEL_PROVIDER_REMOVE = ""',recipe)
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel"',recipe)
        self.assertIn('KERNEL_DEPLOYSUBDIR = "kernel-runtime"',recipe)

    def test_receive_only(self):
        text=(ROOT/"tests/capture-gps.sh").read_text()
        for expected in ("e2900400.serial", "hims,gps-receive-test", "9600 raw -echo",
                         "-crtscts", "timeout 30 cat", "ro_mode"):
            self.assertIn(expected,text)
        self.assertNotIn('> "/dev/',text)
        self.assertNotIn("gpsd",text)
    def test_startup_capture_scope(self):
        text=(ROOT/"tests/capture-gps-startup.sh").read_text()
        for expected in ("hims,gps-receive-test", "ro_mode", "kernel/tainted",
                         "trap restore EXIT", "h432b-gps-power",
                         'wait "$reader"'):
            self.assertIn(expected,text)
        self.assertLess(text.index("--receive-30s"),text.index('> "$driver/unbind"'))
        self.assertLess(text.index('> "$driver/unbind"'),text.rindex('> "$driver/bind"'))
        self.assertNotIn("/dev/mem",text)
        self.assertNotIn("PMTK605",text)
    def test_single_version_query(self):
        text=(ROOT/"tests/query-gps-version.sh").read_text()
        for expected in ("--query-version", "hims,gps-query-test", "e2900400.serial",
                         "ro_mode", "9600 raw -echo", "Refusing console UART"):
            self.assertIn(expected,text)
        command = bytes.fromhex("24504d544b3630352a33315c725c6e").decode()
        self.assertIn(command,text)
        self.assertEqual(text.count('> "/dev/$tty"'),1)
        self.assertLess(text.index("timeout 10 cat"),text.index(command))
    def test_epo_query(self):
        text=(ROOT/"tests/query-gps-epo.sh").read_text()
        self.assertIn("--query-epo",text)
        self.assertIn("$PMTK607*33",text)
        self.assertIn("hims,gps-query-test",text)
        self.assertIn("pidof gpsd",text)
        self.assertEqual(text.count('> "/dev/$tty"'),1)
        self.assertNotIn("PMTK127",text)
if __name__=="__main__":
    unittest.main()
