# SPDX-License-Identifier: MIT
"""Keep the operator guide aligned with the diagnostic ABI, without hardware."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
GUIDE = (ROOT / "docs/wifi.md").read_text()
HISTORY = (ROOT / "docs/wifi-qualification.md").read_text()
DRIVER = (ROOT / "recipes-kernel/linux/files/h432b-wifi-transport.c").read_text()


class WifiDocumentation(unittest.TestCase):
    def test_documented_command_actions_match_abi(self):
        store = DRIVER.split("static ssize_t command_test_store(", 1)[1]
        store = store.split("static DEVICE_ATTR_WO(command_test)", 1)[0]
        accepted = set(re.findall(r'sysfs_streq\(buf, "([^"]+)"\)', store))
        table = GUIDE.split("### Command actions", 1)[1].split(
            "Normal-command replies", 1)[0]
        documented = set(re.findall(r"^\| `([^`]+)` \|", table, re.MULTILINE))
        self.assertEqual(accepted, documented)

    def test_guide_and_history_are_separate(self):
        self.assertIn("(wifi-qualification.md)", GUIDE)
        self.assertIn("(wifi.md)", HISTORY)
        self.assertIn("not current operating instructions", HISTORY)
        self.assertNotIn("SHA256", GUIDE)

    def test_limitations_remain_explicit(self):
        for phrase in ("no `wlan0`", "empty wakeups",
                       "does not contain, fetch or redistribute",
                       "do not emulate the radio",
                       "mutually exclusive"):
            self.assertIn(phrase, GUIDE)


if __name__ == "__main__":
    unittest.main()
