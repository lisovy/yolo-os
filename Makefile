# Makefile for YOLO-OS (bare-metal x86)
#
# Required tools (Debian/Ubuntu):
#   sudo apt install nasm gcc gcc-multilib binutils qemu-system-x86 dosfstools mtools

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
           -nostdlib -nostdinc -O2 -Wall -Wextra -DDEBUG

LDFLAGS := -m elf_i386 -T kernel/linker.ld

# All generated files go here; source directories stay clean.
BUILD := build

# Output files
DISK_IMG  := disk.img
BOOT_IDE  := $(BUILD)/boot_ide.bin
KERNEL    := $(BUILD)/kernel.bin
KELF      := $(BUILD)/kernel.elf

# Kernel object files
KOBJS := $(BUILD)/entry.o $(BUILD)/isr.o $(BUILD)/idt.o \
         $(BUILD)/kernel.o $(BUILD)/fat16.o $(BUILD)/pmm.o

# User programs (flat binaries installed to /bin on FAT16)
# To add a new program: add its .bin to USER_BINS and write a build rule below.
USER_BINS := $(BUILD)/sh.bin $(BUILD)/hello.bin $(BUILD)/xxd.bin $(BUILD)/vi.bin \
             $(BUILD)/demo.bin $(BUILD)/t_segflt.bin \
             $(BUILD)/ls.bin $(BUILD)/rm.bin $(BUILD)/mkdir.bin $(BUILD)/mv.bin \
             $(BUILD)/t_panic.bin $(BUILD)/free.bin \
             $(BUILD)/t_mall1.bin $(BUILD)/t_mall2.bin

# ======================================================================
.PHONY: all run clean newdisk test

all: $(DISK_IMG)

$(BUILD):
	mkdir -p $@

# --- User programs ----------------------------------------------------

$(BUILD)/hello.o: bin/hello.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hello.elf: $(BUILD)/hello.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/hello.bin: $(BUILD)/hello.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/xxd.o: bin/xxd.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/xxd.elf: $(BUILD)/xxd.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/xxd.bin: $(BUILD)/xxd.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/vi.o: bin/vi.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vi.elf: $(BUILD)/vi.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/vi.bin: $(BUILD)/vi.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/demo.o: bin/demo.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/demo.elf: $(BUILD)/demo.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/demo.bin: $(BUILD)/demo.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/t_segflt.o: bin/t_segflt.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/t_segflt.elf: $(BUILD)/t_segflt.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/t_segflt.bin: $(BUILD)/t_segflt.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/sh.o: bin/sh.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sh.elf: $(BUILD)/sh.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/sh.bin: $(BUILD)/sh.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/ls.o: bin/ls.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ls.elf: $(BUILD)/ls.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/ls.bin: $(BUILD)/ls.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/rm.o: bin/rm.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rm.elf: $(BUILD)/rm.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/rm.bin: $(BUILD)/rm.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/mkdir.o: bin/mkdir.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mkdir.elf: $(BUILD)/mkdir.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/mkdir.bin: $(BUILD)/mkdir.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/mv.o: bin/mv.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mv.elf: $(BUILD)/mv.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/mv.bin: $(BUILD)/mv.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/t_panic.o: bin/t_panic.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/t_panic.elf: $(BUILD)/t_panic.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/t_panic.bin: $(BUILD)/t_panic.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/free.o: bin/free.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/free.elf: $(BUILD)/free.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/free.bin: $(BUILD)/free.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/t_mall1.o: bin/t_mall1.c bin/os.h bin/malloc.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/t_mall1.elf: $(BUILD)/t_mall1.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/t_mall1.bin: $(BUILD)/t_mall1.elf
	$(OBJCPY) -O binary $< $@

$(BUILD)/t_mall2.o: bin/t_mall2.c bin/os.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/t_mall2.elf: $(BUILD)/t_mall2.o bin/user.ld
	$(LD) -m elf_i386 -T bin/user.ld $< -o $@

$(BUILD)/t_mall2.bin: $(BUILD)/t_mall2.elf
	$(OBJCPY) -O binary $< $@

# --- Bootloader -------------------------------------------------------

$(BOOT_IDE): boot/boot_ide.asm | $(BUILD)
	$(NASM) -f bin -DKERNEL_SECTORS=$(KERNEL_SECTORS) $< -o $@

# --- Kernel -----------------------------------------------------------

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/isr.o: kernel/isr.asm | $(BUILD)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/idt.o: kernel/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fat16.o: kernel/fat16.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pmm.o: kernel/pmm.c kernel/pmm.h | $(BUILD)
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

$(DISK_IMG): $(KERNEL) $(BOOT_IDE) $(USER_BINS)
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
	mmd -i $@ ::bin 2>/dev/null || true
	@for f in $(USER_BINS); do \
	    name=$$(basename "$$f" .bin); \
	    mcopy -o -i $@ "$$f" "::bin/$$name"; \
	    echo "[disk] installed bin/$$name"; \
	done
	@echo "[disk] $(DISK_IMG) updated (boot + kernel + user programs)"

newdisk:
	rm -f $(DISK_IMG)
	$(MAKE) $(DISK_IMG)

# --- Run in QEMU ------------------------------------------------------

run: $(DISK_IMG)
	$(QEMU) \
	  -drive file=$(DISK_IMG),format=raw,if=ide \
	  -serial stdio \
	  -boot c

# --- Automated tests --------------------------------------------------

test: $(DISK_IMG)
	python3 tests/run_tests.py --disk $(DISK_IMG)

# --- Cleanup ----------------------------------------------------------
# disk.img is intentionally NOT removed by clean (holds persistent data).

clean:
	rm -rf $(BUILD)
