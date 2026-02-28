#!/usr/bin/env python3
"""Generate docs/disk-layout.png and docs/memory-layout.png."""

from PIL import Image, ImageDraw, ImageFont
import os

# ── helpers ───────────────────────────────────────────────────────────────────

def load_font(size):
    for name in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                 "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"]:
        if os.path.exists(name):
            return ImageFont.truetype(name, size)
    return ImageFont.load_default()

def load_font_regular(size):
    for name in ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                 "/usr/share/fonts/TTF/DejaVuSans.ttf"]:
        if os.path.exists(name):
            return ImageFont.truetype(name, size)
    return ImageFont.load_default()

def text_center(draw, cx, cy, text, font, color):
    bbox = draw.textbbox((0, 0), text, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    draw.text((cx - w // 2, cy - h // 2), text, font=font, fill=color)

def draw_block(draw, x1, y1, x2, y2, fill, outline, title, subtitle, fb, fs):
    draw.rounded_rectangle([x1, y1, x2, y2], radius=6, fill=fill, outline=outline, width=2)
    cx = (x1 + x2) // 2
    if subtitle:
        text_center(draw, cx, (y1 + y2) // 2 - 12, title, fb, "white")
        text_center(draw, cx, (y1 + y2) // 2 + 14, subtitle, fs, "white")
    else:
        text_center(draw, cx, (y1 + y2) // 2, title, fb, "white")

# ── disk-layout.png ───────────────────────────────────────────────────────────

def make_disk_layout():
    W, H = 700, 690
    img = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb = load_font(20)
    fs = load_font_regular(16)
    ft = load_font(26)
    fl = load_font_regular(15)

    text_center(draw, W // 2, 36, "disk.img layout  (4 MB raw, 512-byte sectors)", ft, "#222")

    LX = 100
    RX = 600
    LBL = 88

    # Sector 0
    draw_block(draw, LX, 80, RX, 160, "#4caf50", "#388e3c",
               "Sector 0", "Boot sector  (MBR + FAT16 BPB)  512 B", fb, fs)
    draw.text((LBL - draw.textbbox((0,0),"LBA 0",font=fl)[2], 112), "LBA 0", font=fl, fill="#555")

    # Sectors 1-128
    draw_block(draw, LX, 172, RX, 312, "#2196f3", "#1565c0",
               "Sectors 1 – 128", "Kernel binary  (64 KB reserved)", fb, fs)
    draw.text((LBL - draw.textbbox((0,0),"LBA 1",font=fl)[2], 234), "LBA 1", font=fl, fill="#555")

    # FAT16 outer
    draw.rounded_rectangle([LX, 324, RX, 638], radius=6, fill="#ff9800", outline="#e65100", width=2)
    text_center(draw, (LX+RX)//2, 344, "Sectors 129+", fb, "white")
    text_center(draw, (LX+RX)//2, 366, "FAT16 filesystem  (~3.9 MB)", fs, "white")
    draw.text((LBL - draw.textbbox((0,0),"LBA 129",font=fl)[2], 390), "LBA 129", font=fl, fill="#555")

    # inner FAT boxes
    draw.rounded_rectangle([LX+16, 382, RX-16, 422], radius=4, fill="#e65100", outline="#bf360c", width=1)
    text_center(draw, (LX+RX)//2, 402, "FAT table(s)", fs, "white")

    draw.rounded_rectangle([LX+16, 430, RX-16, 470], radius=4, fill="#e65100", outline="#bf360c", width=1)
    text_center(draw, (LX+RX)//2, 450, "Root directory  ( BOOT.TXT    bin/ )", fs, "white")

    draw.rounded_rectangle([LX+16, 478, RX-16, 626], radius=4, fill="#bf360c", outline="#8d1a00", width=1)
    text_center(draw, (LX+RX)//2, 503, "Data clusters", fs, "white")
    text_center(draw, (LX+RX)//2, 526, "BOOT.TXT", fl, "#ffccbc")
    text_center(draw, (LX+RX)//2, 548, "bin/  →  sh   hello   xxd   vi   demo", fl, "#ffccbc")
    text_center(draw, (LX+RX)//2, 568, "       segfault   ls   rm   mkdir   mv", fl, "#ffccbc")
    text_center(draw, (LX+RX)//2, 588, "       panic   free", fl, "#ffccbc")

    text_center(draw, W // 2, 666, "(heights not to scale)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "disk-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── memory-layout.png ─────────────────────────────────────────────────────────

def make_memory_layout():
    W, H = 800, 1040
    img = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb  = load_font(17)
    fs  = load_font_regular(14)
    ft  = load_font(24)
    fl  = load_font_regular(13)
    fh  = load_font(14)

    text_center(draw, W // 2, 34, "Memory layout  (physical + virtual)", ft, "#222")

    LX   = 200
    RX   = 580
    LADDR = 188

    def addr_label(y, text):
        bbox = draw.textbbox((0, 0), text, font=fl)
        draw.text((LADDR - (bbox[2]-bbox[0]), y - (bbox[3]-bbox[1])//2), text, font=fl, fill="#555")

    def ring_label(y, text, color):
        draw.text((RX + 10, y - 8), text, font=fl, fill=color)

    def section_header(y, text, color):
        draw.line([(LX, y+2), (RX, y+2)], fill=color, width=2)
        text_center(draw, (LX+RX)//2, y + 18, text, fh, color)

    # ── physical: kernel / hardware ───────────────────────────────────────────
    section_header(58, "Physical: kernel + hardware  (0x000000 – 0x0FFFFF)", "#424242")

    kern_rows = [
        (78,  38, "#9e9e9e", "#616161", "IVT / BIOS data",           "",   "0x00000",  "ring 0",   "#b71c1c"),
        (122, 38, "#9e9e9e", "#616161", "MBR bootloader  (512 B)",    "",   "0x07C00",  "ring 0",   "#b71c1c"),
        (166, 50, "#29b6f6", "#0277bd", "Kernel  (~16 KB)",           "",   "0x10000",  "ring 0",   "#b71c1c"),
        (222, 34, "#eeeeee", "#bdbdbd", "free RAM  (supervisor)",     "",   "0x14000",  "ring 0",   "#b71c1c"),
        (262, 38, "#ef6c00", "#bf360c", "Kernel stack  (grows ↓)",    "",   "0x90000",  "ring 0",   "#b71c1c"),
        (306, 34, "#6d4c41", "#4e342e", "VGA graphics  (Mode 13h)",   "",   "0xA0000",  "ring 0+3", "#1b5e20"),
        (346, 34, "#6d4c41", "#4e342e", "VGA text framebuffer",       "",   "0xB8000",  "ring 0+3", "#1b5e20"),
    ]
    for (y, h, fill, outline, title, subtitle, addr, ring_txt, ring_color) in kern_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, subtitle, fb, fs)
        addr_label(y + h//2, addr)
        ring_label(y + h//2 - 4, ring_txt, ring_color)

    # ── physical: PMM dynamic region ──────────────────────────────────────────
    PM_Y = 405
    section_header(PM_Y - 10,
        "Physical: PMM dynamic  (0x100000 – 0x7FFFFFFF · ~127 MB · 32 512 frames)",
        "#00695c")

    # PMM container box
    draw.rounded_rectangle([LX, PM_Y + 22, RX, PM_Y + 318],
                            radius=6, fill="#e0f2f1", outline="#00695c", width=2)
    addr_label(PM_Y + 30, "0x100000")

    # per-process breakdown inside container
    text_center(draw, (LX+RX)//2, PM_Y + 38,
                "per-process allocation  (~300 kB each)", fh, "#004d40")

    pmm_rows = [
        (PM_Y+ 56, 26, "#00897b", "#004d40", "page dir + page table   2 × 4 kB = 8 kB"),
        (PM_Y+ 88, 68, "#43a047", "#2e7d32", "binary                 64 × 4 kB = 256 kB"),
        (PM_Y+162, 36, "#f57c00", "#e65100", "stack + args            7 × 4 kB = 28 kB"),
        (PM_Y+204, 26, "#5e35b1", "#311b92", "kernel stack            1 × 4 kB = 4 kB"),
    ]
    for (y, h, fill, outline, title) in pmm_rows:
        draw.rounded_rectangle([LX+14, y, RX-14, y+h],
                                radius=4, fill=fill, outline=outline, width=1)
        text_center(draw, (LX+RX)//2, y + h//2, title, fs, "white")

    text_center(draw, (LX+RX)//2, PM_Y + 248, "= 75 frames total per process", fh, "#00695c")
    text_center(draw, (LX+RX)//2, PM_Y + 274, "× up to ~430 simultaneous processes", fl, "#00695c")
    text_center(draw, (LX+RX)//2, PM_Y + 296, "unlimited nesting depth  ·  freed on exit", fl, "#00695c")

    # ── virtual address space per process ─────────────────────────────────────
    VY = PM_Y + 342
    section_header(VY - 10,
        "Virtual address space per process  (all link at 0x400000)",
        "#1565c0")

    virt_rows = [
        (VY+ 12, 40, "#9e9e9e", "#616161", "kernel  (supervisor only)",   "0x000000", "ring 0",   "#b71c1c"),
        (VY+ 58, 44, "#43a047", "#2e7d32", "binary  256 kB",              "0x400000", "ring 3",   "#1565c0"),
        (VY+108, 28, "#e0e0e0", "#bdbdbd", "unmapped  (∼3.7 MB)",         "0x440000", "",         "#888888"),
        (VY+142, 28, "#7b1fa2", "#4a148c", "args  (ARGS_BASE = 0x7FC000)","0x7FC000", "ring 3",   "#1565c0"),
        (VY+176, 36, "#f57c00", "#e65100", "stack  28 kB  (grows ↓)",     "0x7FF000", "ring 3",   "#1565c0"),
    ]
    for (y, h, fill, outline, title, addr, ring_txt, ring_color) in virt_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, "", fb, fs)
        addr_label(y + h//2, addr)
        if ring_txt:
            ring_label(y + h//2 - 4, ring_txt, ring_color)

    text_center(draw, W // 2, H - 20, "(heights not to scale)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "memory-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    make_disk_layout()
    make_memory_layout()
