# SPDX-License-Identifier: MIT
from pathlib import Path
import tempfile
import textwrap
import unittest

ROOT=Path(__file__).resolve().parents[1]
BODY=(ROOT/"classes/h432b-systembase.bbclass").read_text().split(
    "python h432b_check_systembase_size() {\n",1)[1].split("\n}",1)[0]
CODE=compile(textwrap.dedent(BODY),"<h432b-systembase>","exec")

class Fatal:
    @staticmethod
    def fatal(message): raise ValueError(message)

class Data:
    def __init__(self,values): self.values=values
    def getVar(self,key): return self.values[key]

class SystembaseLimit(unittest.TestCase):
    def run_guard(self,size=None,lebs=1651,limit=209715200):
        with tempfile.TemporaryDirectory() as tmp:
            if size is not None:
                with (Path(tmp)/"base.squashfs-xz").open("wb") as f: f.truncate(size)
            values={"IMGDEPLOYDIR":tmp,"H432B_SYSTEMBASE_MAX_BYTES":str(limit),
                    "H432B_SYSTEMBASE_LEBS":str(lebs),"H432B_UBI_LEB_BYTES":"126976"}
            exec(CODE,{"bb":Fatal,"d":Data(values)})
    def test_capacity_fits(self): self.run_guard(1651*126976)
    def test_one_byte_over_capacity_fails(self):
        with self.assertRaises(ValueError): self.run_guard(1651*126976+1)
    def test_missing_artifact_fails(self):
        with self.assertRaises(ValueError): self.run_guard()
    def test_override_cannot_raise_200mib(self):
        with self.assertRaises(ValueError): self.run_guard(1,1652,400*1024*1024)
    def test_smaller_policy_is_respected(self):
        with self.assertRaises(ValueError): self.run_guard(1,1651,100*1024*1024)
    def test_machine_imports_budget(self):
        self.assertIn("require conf/machine/include/h432b-storage.inc",
                      (ROOT/"conf/machine/h432b.conf").read_text())

if __name__=="__main__": unittest.main()
