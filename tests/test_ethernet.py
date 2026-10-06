# SPDX-License-Identifier: MIT
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "recipes-support/h432b-ethernet-probe"
class EthernetProbeContract(unittest.TestCase):
    def test_driver_and_board_wiring(self):
        dts = (ROOT / "recipes-kernel/linux/files/s5pv210-hims-u2.dts").read_text()
        node = dts.split("ethernet: ethernet@a8000000 {", 1)[1].split("};", 1)[0]
        for prop in ('"smsc,lan9220", "smsc,lan9115"', "reg-io-width = <2>",
                     "IRQ_TYPE_LEVEL_LOW", "CLK_SROMC",
                     "reset-gpios = <&gph1 2 GPIO_ACTIVE_LOW>", 'status = "okay"'):
            self.assertIn(prop, node)
        self.assertNotIn("smsc,irq-active-high", node)
        self.assertNotIn("smsc,irq-push-pull", node)
        self.assertIn('"mp01-5", "mp01-6", "mp01-7"', dts)
        config = (ROOT / "recipes-kernel/linux/files/u2-ethernet.config").read_text()
        self.assertIn("CONFIG_SMSC911X=y", config)
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b_6.12.111.bb").read_text()
        self.assertIn("${UNPACKDIR}/u2-ethernet.config", recipe)
    def test_read_only(self):
        source = (BASE / "files/ethernet-probe.c").read_text()
        self.assertIn('open("/dev/mem", O_RDONLY | O_SYNC)', source)
        self.assertIn("PROT_READ, MAP_SHARED", source)
        self.assertNotIn("PROT_WRITE", source)
        self.assertNotIn("O_RDWR", source)
        self.assertIn("volatile const uint32_t *reg", source)
    def test_fixed_register_allowlist(self):
        source = (BASE / "files/ethernet-probe.c").read_text()
        addresses = {int(x, 16) for x in re.findall(r'{(0x[0-9a-f]+), "', source)}
        self.assertEqual(addresses, {0xe02002e0, 0xeee10464, 0xe8000000, 0xe8000018, 0xe0200c20,
            0xe0200c24, 0xa8000064, 0xa8000050, 0xa8000054, 0xa800005c,
            0xa8000074, 0xa8000084})
        self.assertTrue(all(x % 4 == 0 for x in addresses))
        self.assertIn("value != 0x87654321", source)
    def test_mux_test_is_explicit_and_restores_one_nibble(self):
        source = (BASE / "files/ethernet-mux-test.c").read_text()
        self.assertIn('"--test-and-restore"', source)
        self.assertIn("#define MUX_MASK (0xfU << 20)", source)
        self.assertIn("(saved & MUX_MASK) != (5U << 20)", source)
        self.assertIn("*mux = (*mux & ~MUX_MASK) | (saved & MUX_MASK);", source)
        self.assertEqual(source.count("*mux = ("), 3)  # declaration plus two writes
        self.assertIn("PROT_READ, MAP_SHARED, fd, 0xa8000000", source)
    def test_recipe_license_checksum(self):
        recipe = (BASE / "h432b-ethernet-probe_1.0.bb").read_text()
        self.assertRegex(recipe, r'md5=[0-9a-f]{32}"')
if __name__ == "__main__":
    unittest.main()
