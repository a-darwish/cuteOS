#!/usr/bin/env python
#
# Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, version 2.
#
# Usage: $script
#        dd if=data/hd-image of=/dev/disk/by-label/$CUTE-DISK-LABEL
#
# Build the Cute kernel final image, which is going to be directly
# written to disk. That final image is a concatenation of:
#  o a (possibly extended) 512-KB Kernel image
#  o and, a 24-byte ramdisk header
#  o and, the Ramdisk image itself (if any)
#
# The 24-byte Ramdisk header consists of:
#  o start signature, "CUTE-STA", 8-bytes
#  o ramdisk size in number of 512-byte sectors, 4-bytes
#  o ramdisk length (minus the header) in bytes, 4-bytes
#  o end signature, "CUTE-END", 8-bytes
#
# As seen above, ramdisk length is expressed in two terms:
# o in 512-byte sectors (including header len), for the real-mode
#   assembly code loading the header and its ramdisk image to RAM.
# o in plain bytes (without  header len), for the C code accssing
#   the ramdisk image directly.
#
# Python-2.7 _AND_ Python-3.0+ compatible
# NOTE! Always read & write the files in binary mode.
#

import sys
import os.path
import struct

kernel_folder = 'kern/'
build_folder  = 'build/'
kernel_file   = kernel_folder + 'image'
ramdisk_file  = build_folder  + 'ramdisk'
final_image   = build_folder  + 'hd-image'

if not os.path.exists(build_folder):
    os.makedirs(build_folder)

# Expand kernel image to 512 Kbytes
size_512k = 512 * 1024
with open(kernel_file, 'rb') as f:
    buf1 = f.read()
with open(final_image, 'wb') as f:
    f.write(buf1)
    f.write(b'0' * size_512k)
    f.truncate(size_512k)

# Build ramdisk header, and its buffer
header_length = 8 + 4 + 4 + 8
ramdisk_length = header_length
if os.path.exists(ramdisk_file):
    ramdisk_length += os.path.getsize(ramdisk_file)
    ramdisk_buffer  = open(ramdisk_file, 'rb').read()
else:
    ramdisk_buffer  = b''
ramdisk_sectors = (ramdisk_length - 1)//512 + 1
ramdisk_header = struct.pack('=8cII8c',
    b'C', b'U', b'T', b'E', b'-', b'S', b'T', b'A',
    ramdisk_sectors,
    ramdisk_length - header_length,
    b'C', b'U', b'T', b'E', b'-', b'E', b'N', b'D')
assert len(ramdisk_header) == header_length

# Expand final disk image beyond 1MB: some virtual
# machine BIOSes fail if that image len is < 1MB
size_1MB = 1024 * 1024
with open(final_image, 'ab') as f:
    f.write(ramdisk_header)
    f.write(ramdisk_buffer)
    f.write(b'#' * size_1MB)
