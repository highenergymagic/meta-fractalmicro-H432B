# SPDX-License-Identifier: MIT
"""Scope guards, not electrical qualification."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class UsbPowerToolTests(unittest.TestCase):
    def test_bounded_controls(self):
        text=(ROOT/"recipes-support/h432b-usb-power-test/files/usb-power-test.c").read_text()
        for bank,pin in (("gph2",7),("gph3",7),("gph1",3),("gpj4",2)):
            self.assertIn('{"%s", %d, -1, 0}'%(bank,pin),text)
        for forbidden in ("gpe1","gpj0","/dev/mtd","PROT_WRITE","/dev/i2c"):
            self.assertNotIn(forbidden,text)
        self.assertIn("PROT_READ,MAP_SHARED",text)
        self.assertIn("if(snapshot(1))",text)
        self.assertIn("GPIO_V2_LINE_FLAG_USED",text)
        self.assertIn(".tv_sec=20",text)
        self.assertIn("sigaction(SIGHUP",text)
        self.assertIn("write_value(&controls[i],controls[i].saved)",text)
        self.assertIn("for(int i=(int)ncontrols-1;i>=0;i--)",text)
        self.assertIn(".tv_nsec=100000000",text)
        self.assertIn("write_value(&controls[3],1)",text)
        self.assertLess(text.index("write_value(&controls[3],0)"), text.index(".tv_nsec=100000000"))
        self.assertLess(text.index(".tv_nsec=100000000"), text.index("write_value(&controls[3],1)"))
        self.assertIn("if(!enable) return host_snapshot() ? 1 : 0;",text)
        self.assertIn("configure_bias(&controls[i],saved_bias[i])",text)
        self.assertIn("GPIO_V2_LINE_FLAG_BIAS_DISABLED",text)
        self.assertIn("const unsigned counts[]={3,7,8}",text)
    def test_pinned_recipe(self):
        text=(ROOT/"recipes-support/h432b-usb-power-test/h432b-usb-power-test_1.0.bb").read_text()
        self.assertIn("${CC} ${CFLAGS}",text)
        self.assertIn("-Wall -Wextra -Werror",text)
if __name__=="__main__":
    unittest.main()
