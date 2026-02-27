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
OS_IMG   := os.img
DISK_IMG := disk.img
BOOT     := boot/boot.bin
KERNEL   := kernel/kernel.bin
KELF     := kernel/kernel.elf

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

# --- Floppy image -----------------------------------------------------
# Sector 0 (512 B) : bootloader
# Sectors 1+       : kernel (loaded to 0x10000)

$(OS_IMG): $(BOOT) $(KERNEL)
	cat $(BOOT) $(KERNEL) > $@
	truncate -s 1474560 $@   # pad to standard 1.44 MB floppy size

# --- IDE disk image ---------------------------------------------------
# Created once; NOT removed by 'clean' because it holds persistent data.
# Delete manually if you want a fresh disk: rm -f $(DISK_IMG)

$(DISK_IMG):
	dd if=/dev/zero of=$@ bs=1M count=4 2>/dev/null

# --- Run in QEMU ------------------------------------------------------

run: $(OS_IMG) $(DISK_IMG)
	$(QEMU) \
	  -drive file=$(OS_IMG),format=raw,if=floppy \
	  -drive file=$(DISK_IMG),format=raw,if=ide \
	  -serial stdio

# --- Cleanup ----------------------------------------------------------
# Note: $(DISK_IMG) is intentionally NOT removed here.

clean:
	rm -f $(BOOT) $(KOBJS) $(KELF) $(KERNEL) $(OS_IMG)
