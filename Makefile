# Makefile for a simple x86 bare-metal OS
#
# Required tools (Debian/Ubuntu):
#   sudo apt install nasm gcc gcc-multilib binutils qemu-system-x86

NASM   := nasm
CC     := gcc
LD     := ld
OBJCPY := objcopy
QEMU   := qemu-system-i386

# 32-bit freestanding build (no libc, no stdlib)
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector \
           -nostdlib -nostdinc -O2 -Wall -Wextra

LDFLAGS := -m elf_i386 -T kernel/linker.ld

# Output files
OS_IMG  := os.img
BOOT    := boot/boot.bin
KERNEL  := kernel/kernel.bin
KELF    := kernel/kernel.elf

# Kernel object files
KOBJS   := kernel/entry.o kernel/kernel.o

# ======================================================================
.PHONY: all run clean

all: $(OS_IMG)

# --- Bootloader -------------------------------------------------------

$(BOOT): boot/boot.asm
	$(NASM) -f bin $< -o $@

# --- Kernel -----------------------------------------------------------

kernel/entry.o: kernel/entry.asm
	$(NASM) -f elf32 $< -o $@

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link to ELF first, then convert to a flat binary
$(KELF): $(KOBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(KOBJS) -o $@

$(KERNEL): $(KELF)
	$(OBJCPY) -O binary $< $@

# --- Disk image -------------------------------------------------------
# 1.44 MB floppy image:
#   Sector 0 (512 B) : bootloader
#   Sectors 1+       : kernel (loaded to 0x10000)

$(OS_IMG): $(BOOT) $(KERNEL)
	cat $(BOOT) $(KERNEL) > $@
	truncate -s 1474560 $@   # pad to standard 1.44 MB floppy size

# --- Run in QEMU ------------------------------------------------------

run: $(OS_IMG)
	$(QEMU) -fda $(OS_IMG)

# --- Cleanup ----------------------------------------------------------

clean:
	rm -f $(BOOT) $(KOBJS) $(KELF) $(KERNEL) $(OS_IMG)
