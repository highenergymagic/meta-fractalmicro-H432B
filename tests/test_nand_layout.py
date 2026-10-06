# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
P=Path(__file__).resolve().parents[1]/"scripts/nand-layout.py"
spec=importlib.util.spec_from_file_location("layout",P)
layout=importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class NandLayout(unittest.TestCase):
    def test_base_cap_and_shared_reserve(self):
        p=layout.plan()
        for v in p["volumes"]:
            if v["name"].startswith("systembase"):
                self.assertEqual(v["lebs"],1651)
                self.assertLessEqual(v["max_image_bytes"],200*(1<<20))
        self.assertEqual(p["linux_ubi"]["size"],507*(1<<20))
        self.assertEqual(p["future_bad_reserve_pebs"],80)
        self.assertEqual(p["free_pool_lebs_after_volumes"],138)

    def test_does_not_spend_bad_block_headroom_twice(self):
        self.assertEqual(layout.plan(10)["free_pool_lebs_after_volumes"],128)
        self.assertEqual(layout.plan(138)["free_pool_lebs_after_volumes"],0)
        with self.assertRaises(ValueError): layout.plan(139)
        with self.assertRaises(ValueError): layout.plan(-1)

    def test_physical_regions_cover_device_and_align(self):
        p=layout.plan()
        regions=[p[k] for k in ("factory_boot","linux_ubi","bbt_reserve")]
        end=0
        for r in regions:
            self.assertEqual(r["offset"],end)
            self.assertEqual(r["size"]%layout.PEB,0)
            end+=r["size"]
        self.assertEqual(end,512*(1<<20))


if __name__=="__main__": unittest.main()
