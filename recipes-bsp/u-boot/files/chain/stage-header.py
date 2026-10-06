#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate a high-RAM second stage and generate build-tree size/CRC metadata."""
import argparse
from pathlib import Path
import struct
import zlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("binary",type=Path)
p.add_argument("header",type=Path)
a=p.parse_args()
b=a.binary.read_bytes()
if not 68<=len(b)<=0x80000 or len(b)%4 or b[3]!=0xea or struct.unpack_from("<I",b,0x40)[0]!=0x46000000:
    raise ValueError("not the bounded 46000000 ARM stage")
a.header.write_text("#define U2_STAGE_BYTES %dU\n#define U2_STAGE_CRC 0x%08xU\n" % (len(b),zlib.crc32(b)))
