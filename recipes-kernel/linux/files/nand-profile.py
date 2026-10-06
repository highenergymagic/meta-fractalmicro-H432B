#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select a fixed H432B qualification window in a build-tree DTS; no hardware."""
import argparse
from pathlib import Path
WINDOWS={"readonly":None,"scratch":(0x1fee0000,0x20000),"ubi":(0x400000,0x1fb00000)}

def apply(text,profile):
    marker='compatible = "hims,u2-nand-bch";'
    if text.count(marker)!=1 or "hims,write-window" in text:
        raise ValueError("Expected unmodified single H432B NAND node")
    if profile not in WINDOWS: raise ValueError("Invalid NAND profile")
    window=WINDOWS[profile]
    if window is not None:
        text=text.replace(marker,marker+'\n\t\thims,write-window = <0x%08x 0x%08x>;' % window)
    return text

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--profile",choices=WINDOWS,required=True)
    p.add_argument("dts",type=Path)
    a=p.parse_args()
    a.dts.write_text(apply(a.dts.read_text(),a.profile))
