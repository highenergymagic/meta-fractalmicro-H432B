#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the fetched BCH source twice and compare fixed/generic behavior."""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="bch-equivalence-") as folder:
        work = Path(folder)
        (work / "linux").mkdir()
        (work / "asm").mkdir()
        (work / "common.h").write_text("""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kzalloc(n,f) calloc(1,n)
#define kfree(p) free(p)
#define printk printf
#define KERN_ERR ""
""")
        (work / "ubi_uboot.h").write_text("")
        (work / "linux/types.h").write_text("#include <stdint.h>\n")
        (work / "linux/bitops.h").write_text(
            "static inline int fls(unsigned x) { return x ? 32-__builtin_clz(x) : 0; }\n")
        (work / "asm/byteorder.h").write_text("""
#include <stdint.h>
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define cpu_to_be32(x) __builtin_bswap32((uint32_t)(x))
#else
#define cpu_to_be32(x) ((uint32_t)(x))
#endif
""")
        shutil.copyfile(args.source / "include/linux/bch.h", work / "linux/bch.h")
        compiler = shlex.split(args.cc)
        flags = ["-std=gnu99", "-O2", "-I" + str(work)]
        for fixed in (False, True):
            name = "fixed" if fixed else "generic"
            defines = []
            if fixed:
                defines = ["-DCONFIG_BCH_CONST_PARAMS", "-DCONFIG_BCH_CONST_M=13",
                           "-DCONFIG_BCH_CONST_T=8"]
                defines += ["-D" + s + "=fixed_" + s for s in
                            ("init_bch", "free_bch", "encode_bch", "decode_bch")]
            subprocess.run(compiler + flags + defines +
                           ["-c", str(args.source / "lib/bch.c"),
                            "-o", str(work / (name + ".o"))], check=True)
        executable = work / "test"
        subprocess.run(compiler + flags + ["-Wall", "-Wextra", "-Werror",
                       str(Path(__file__).with_name("test-bch-equivalence.c")),
                       str(work / "generic.o"), str(work / "fixed.o"),
                       "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()
