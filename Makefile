# Makefile for YOLO-OS (bare-metal x86)
#
# Required tools (Debian/Ubuntu):
#   sudo apt install nasm gcc gcc-multilib binutils qemu-system-x86 dosfstools

NASM   := nasm
CC     := gcc
LD     := ld
OBJCPY := objcopy
QEMU   := qemu-system-i386

# Number of 512-byte sectors reserved for the kernel (sector 0 = boot, sectors 1..N = kernel).
# FAT16 starts at sector N+1.  Changing this single variable updates the bootloader,
# the mkfs.fat reserved-sector count, and the build-time size check.
KERNEL_SECTORS := 128

# 32-bit freestanding build
CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector \
           -nostdlib -nostdinc -O2 -Wall -Wextra

LDFLAGS := -m elf_i386 -T kernel/linker.ld

# Output files
DISK_IMG  := disk.img
BOOT_IDE  := boot/boot_ide.bin
KERNEL    := kernel/kernel.bin
KELF      := kernel/kernel.elf

# Kernel object files
KOBJS := kernel/entry.o kernel/isr.o kernel/idt.o kernel/kernel.o kernel/fat16.o

# ======================================================================
.PHONY: all run clean newdisk

all: $(DISK_IMG)

# --- Bootloader -------------------------------------------------------

$(BOOT_IDE): boot/boot_ide.asm
	$(NASM) -f bin -DKERNEL_SECTORS=$(KERNEL_SECTORS) $< -o $@

# --- Kernel -----------------------------------------------------------

kernel/entry.o: kernel/entry.asm
	$(NASM) -f elf32 $< -o $@

kernel/isr.o: kernel/isr.asm
	$(NASM) -f elf32 $< -o $@

kernel/idt.o: kernel/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/fat16.o: kernel/fat16.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KELF): $(KOBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(KOBJS) -o $@

$(KERNEL): $(KELF)
	$(OBJCPY) -O binary $< $@

# --- IDE disk image ---------------------------------------------------
# disk.img holds both the bootloader/kernel and the FAT16 filesystem.
#
# Layout:
#   Sector 0                    : boot sector  (MBR + FAT16 BPB)
#   Sectors 1..KERNEL_SECTORS   : kernel binary
#   Sectors KERNEL_SECTORS+1 .. : FAT16 structures (FAT tables, root dir, data)
#
# KERNEL_SECTORS is the single variable controlling all three of the above.
# To reformat from scratch (required when KERNEL_SECTORS changes): make newdisk

$(DISK_IMG): $(KERNEL) $(BOOT_IDE)
	@size=$$(wc -c < $(KERNEL)); \
	 max=$$(($(KERNEL_SECTORS) * 512)); \
	 if [ "$$size" -gt "$$max" ]; then \
	   echo "ERROR: kernel $$size bytes > KERNEL_SECTORS=$(KERNEL_SECTORS) * 512 = $$max"; \
	   echo "       Increase KERNEL_SECTORS in Makefile and run: make newdisk"; \
	   exit 1; \
	 fi
	@if [ ! -f $@ ]; then \
	    echo "[disk] creating fresh FAT16 image..."; \
	    dd if=/dev/zero of=$@ bs=1M count=4 2>/dev/null; \
	    mkfs.fat -F 16 -s 1 -n "YOLOOS" -R $$(($(KERNEL_SECTORS) + 1)) $@; \
	fi
	bash scripts/patch_boot.sh $@ $(BOOT_IDE)
	dd if=$(KERNEL) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "[disk] $(DISK_IMG) updated (boot + kernel)"

newdisk:
	rm -f $(DISK_IMG)
	$(MAKE) $(DISK_IMG)

# --- Run in QEMU ------------------------------------------------------

run: $(DISK_IMG)
	$(QEMU) \
	  -drive file=$(DISK_IMG),format=raw,if=ide \
	  -serial stdio \
	  -boot c

# --- Cleanup ----------------------------------------------------------
# disk.img is intentionally NOT removed by clean (holds persistent data).

clean:
	rm -f $(BOOT_IDE) $(KOBJS) $(KELF) $(KERNEL)
