#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plan H432B NAND capacity; never opens hardware or formats storage."""
import argparse
import json

MIB=1<<20
PEB=128<<10
LEB=124<<10
BASE_LIMIT=200*MIB
BOOT_END=4*MIB
BBT_START=511*MIB
TOTAL=512*MIB


def plan(bad_blocks=0):
    if not isinstance(bad_blocks,int) or bad_blocks<0 or bad_blocks>4096:
        raise ValueError("invalid bad-block count")
    pool=(BBT_START-BOOT_END)//PEB
    # Conservative: reserve future bad blocks IN ADDITION to observed bad PEBs.
    # Four more PEBs cover two layout copies plus WL/atomic-change headroom.
    future=(pool*20+1023)//1024
    available=pool-bad_blocks-future-4
    budgets=(("kernel_a",16),("kernel_b",16),("recovery",32),
             ("systembase_a",200),("systembase_b",200))
    volumes=[{"name":name,"type":"static","lebs":size*MIB//LEB,
              "max_image_bytes":size*MIB//LEB*LEB} for name,size in budgets]
    volumes += [{"name":name,"type":"dynamic","lebs":2,
                 "max_image_bytes":2*LEB} for name in ("bootstate_a","bootstate_b")]
    used=sum(v["lebs"] for v in volumes)
    if used>available:
        raise ValueError("A/B layout does not fit after bad-block reserves")
    return {"status":"proposal-not-provisioned","nand_bytes":TOTAL,
            "page_bytes":2048,"oob_bytes":64,"peb_bytes":PEB,"leb_bytes":LEB,
            "factory_boot":{"offset":0,"size":BOOT_END,"readonly":True},
            "linux_ubi":{"offset":BOOT_END,"size":BBT_START-BOOT_END},
            "bbt_reserve":{"offset":BBT_START,"size":TOTAL-BBT_START,"readonly":True},
            "observed_bad_pool_blocks":bad_blocks,"future_bad_reserve_pebs":future,
            "fixed_ubi_reserve_pebs":4,"free_pool_lebs_after_volumes":available-used,
            "base_hard_limit_bytes":BASE_LIMIT,"volumes":volumes,
            "note":"ubiblock maps static volumes; it needs no extra partition"}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bad-blocks",type=int,default=0)
    args=p.parse_args()
    print(json.dumps(plan(args.bad_blocks),indent=2))
