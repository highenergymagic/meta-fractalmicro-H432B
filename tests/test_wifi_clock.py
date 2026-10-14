# SPDX-License-Identifier: MIT
"""Board SDIO clocks must not inherit the audio PLL's sample-rate changes."""
from pathlib import Path
import re
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class WifiClockContract(unittest.TestCase):
    def test_wifi_selects_stable_mpll_before_host_probe(self):
        source = (FILES / "s5pv210-hims-u2.dts").read_text()
        node = re.search(r"&sdhci3\s*\{(.*?)\n\};", source, re.S).group(1)
        self.assertIn("assigned-clocks = <&clocks MOUT_MMC3>, <&clocks DOUT_MMC3>;", node)
        self.assertIn("assigned-clock-parents = <&clocks MOUT_MPLL>, <0>;", node)
        self.assertIn("assigned-clock-rates = <0>, <50000000>;", node)
        self.assertIn("max-frequency = <25000000>;", node)
        self.assertNotIn("<&clocks MOUT_EPLL>", node)


if __name__ == "__main__":
    unittest.main()
