#!/bin/bash
# patch_boot.sh  disk.img  boot_ide.bin
#
# Writes a patched boot sector to sector 0 of disk.img:
#   bytes  0-10:  JMP + NOP + OEM  from boot_ide.bin
#   bytes 11-61:  FAT16 BPB        from disk.img (written by mkfs.fat)
#   bytes 62-509: boot code        from boot_ide.bin
#   bytes 510-511: 0x55 0xAA       boot signature

set -e

DISK="$1"
BOOT="$2"
TMP=$(mktemp)

dd if="$BOOT" bs=1 count=11           of="$TMP" 2>/dev/null  # bytes 0-10
dd if="$DISK" bs=1 skip=11 count=51  >> "$TMP" 2>/dev/null  # bytes 11-61
dd if="$BOOT" bs=1 skip=62 count=448 >> "$TMP" 2>/dev/null  # bytes 62-509
printf '\x55\xAA'                     >> "$TMP"               # bytes 510-511

dd if="$TMP" of="$DISK" bs=512 count=1 conv=notrunc 2>/dev/null
rm -f "$TMP"

echo "[patch_boot] sector 0 of $DISK patched OK"
