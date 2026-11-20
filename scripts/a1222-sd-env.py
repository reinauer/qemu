#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make an SD card image with a U-Boot environment for QEMU's -M a1222.

AmigaOS's nonvolatile.library on the A1222 reads U-Boot's environment from
the SD card (through bootsd.resource and the eSDHC): 8 KiB at block 0x3000,
a big-endian CRC32 of the rest, then NUL-terminated "name=value" strings.
A1222eth.device opens only when it finds its MAC address there, as
"ethaddr" (unit 0) and "eth1addr" (unit 1).

Usage:
  a1222-sd-env.py sd.img [name=value ...]

With no variables, it sets ethaddr and eth1addr to QEMU's default MAC
addresses for the first two NICs.  Then run QEMU with
  -drive if=sd,file=sd.img,format=raw
"""

import struct
import sys
import zlib

ENV_BLOCK = 0x3000
ENV_SIZE = 8192
IMAGE_SIZE = 16 * 1024 * 1024       # QEMU wants a power of 2


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    variables = sys.argv[2:] or ["ethaddr=52:54:00:12:34:56",
                                 "eth1addr=52:54:00:12:34:57"]
    for v in variables:
        if "=" not in v:
            sys.exit("not a name=value pair: " + v)

    data = b"".join(v.encode() + b"\0" for v in variables) + b"\0"
    if len(data) > ENV_SIZE - 4:
        sys.exit("environment too big")
    data = data.ljust(ENV_SIZE - 4, b"\0")
    env = struct.pack(">I", zlib.crc32(data)) + data

    with open(sys.argv[1], "wb") as f:
        f.truncate(IMAGE_SIZE)
        f.seek(ENV_BLOCK * 512)
        f.write(env)


if __name__ == "__main__":
    main()
