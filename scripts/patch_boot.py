#!/usr/bin/env python3
"""
patch_boot.py  disk.img  boot_ide.bin

Combines:
  - bytes  0-10:  JMP + OEM  from boot_ide.bin
  - bytes 11-61:  FAT16 BPB  from disk.img  (written by mkfs.fat)
  - bytes 62-509: boot code  from boot_ide.bin
  - bytes 510-511: 0x55 0xAA (boot signature)

Writes the result back to sector 0 of disk.img.
"""
import sys

disk_path = sys.argv[1]
boot_path = sys.argv[2]

with open(disk_path, "r+b") as fh:
    disk_sector0 = bytearray(fh.read(512))

with open(boot_path, "rb") as fh:
    boot = bytearray(fh.read(512))

result = bytearray(512)
result[0:11]    = boot[0:11]         # JMP + NOP + OEM from our binary
result[11:62]   = disk_sector0[11:62] # BPB from mkfs.fat
result[62:510]  = boot[62:510]        # boot code from our binary
result[510:512] = bytes([0x55, 0xAA]) # boot signature

with open(disk_path, "r+b") as fh:
    fh.write(result)

print(f"[patch_boot] sector 0 of {disk_path} patched OK")
