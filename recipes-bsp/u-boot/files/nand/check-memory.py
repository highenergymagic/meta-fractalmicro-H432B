#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reject overlap between the in-place loader, fixed heap, and derived stack."""
import argparse
import json
import re
from pathlib import Path

def check(text,heap_start=0x46400000,heap_size=0x401000,chain=False):
    symbols=dict((name,int(value,16)) for value,name in
                 re.findall(r"(0x[0-9a-fA-F]+)\s+(__image_copy_start|__bss_end__)\s*=",text))
    start=symbols["__image_copy_start"]
    end=symbols["__bss_end__"]
    expected=0x40021000 if chain else 0x46000000
    limit=0x80000 if chain else 0x100000
    if start!=expected or not start<end<=start+limit:
        raise ValueError("wrong/oversize in-place loader")
    alloc_top=0x40e00000 if chain else 0x46e00000
    if chain:
        heap_start,heap_size=0x40800000,0x101000
    tlb=(alloc_top-16384)&~65535
    reserved_image=(tlb-(end-start))&~4095
    # Reserve an additional 64 KiB for actual stack plus board/global data.
    stack_floor=reserved_image-heap_size-65536
    heap_end=heap_start+heap_size
    if not end<=heap_start<heap_end<=stack_floor:
        raise ValueError("heap overlaps the loader or the live stack/global-data reservation")
    return {"image":[start,end],"heap":[heap_start,heap_end],
            "conservative_stack_floor":stack_floor,"tlb":tlb}
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("map",type=Path)
    p.add_argument("--chain",action="store_true")
    a=p.parse_args()
    print(json.dumps(check(a.map.read_text(),chain=a.chain),sort_keys=True))
