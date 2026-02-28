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
    W, H = 700, 660
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
    draw.rounded_rectangle([LX, 324, RX, 610], radius=6, fill="#ff9800", outline="#e65100", width=2)
    text_center(draw, (LX+RX)//2, 344, "Sectors 129+", fb, "white")
    text_center(draw, (LX+RX)//2, 366, "FAT16 filesystem  (~3.9 MB)", fs, "white")
    draw.text((LBL - draw.textbbox((0,0),"LBA 129",font=fl)[2], 390), "LBA 129", font=fl, fill="#555")

    # inner FAT boxes
    draw.rounded_rectangle([LX+16, 382, RX-16, 422], radius=4, fill="#e65100", outline="#bf360c", width=1)
    text_center(draw, (LX+RX)//2, 402, "FAT table(s)", fs, "white")

    draw.rounded_rectangle([LX+16, 430, RX-16, 470], radius=4, fill="#e65100", outline="#bf360c", width=1)
    text_center(draw, (LX+RX)//2, 450, "Root directory  ( BOOT.TXT    bin/ )", fs, "white")

    draw.rounded_rectangle([LX+16, 478, RX-16, 598], radius=4, fill="#bf360c", outline="#8d1a00", width=1)
    text_center(draw, (LX+RX)//2, 503, "Data clusters", fs, "white")
    text_center(draw, (LX+RX)//2, 526, "BOOT.TXT", fl, "#ffccbc")
    text_center(draw, (LX+RX)//2, 547, "bin/  →  sh   hello   xxd   vi   demo", fl, "#ffccbc")
    text_center(draw, (LX+RX)//2, 568, "       segfault   ls   rm   mkdir   mv", fl, "#ffccbc")

    text_center(draw, W // 2, 638, "(heights not to scale)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "disk-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── memory-layout.png ─────────────────────────────────────────────────────────

def make_memory_layout():
    W, H = 780, 1020
    img = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb  = load_font(17)
    fs  = load_font_regular(14)
    ft  = load_font(24)
    fl  = load_font_regular(13)
    fh  = load_font(14)

    text_center(draw, W // 2, 34, "Physical memory layout  (running in QEMU)", ft, "#222")

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

    # ── kernel / hardware ─────────────────────────────────────────────────────
    rows = [
        (64,  44, "#9e9e9e", "#616161", "IVT / BIOS data",          "",               "0x00000",  "ring 0",   "#b71c1c"),
        (116, 44, "#9e9e9e", "#616161", "MBR bootloader  (512 B)",   "",               "0x07C00",  "ring 0",   "#b71c1c"),
        (168, 60, "#29b6f6", "#0277bd", "Kernel  (~16 KB)",          "",               "0x10000",  "ring 0",   "#b71c1c"),
        (236, 44, "#eeeeee", "#bdbdbd", "free RAM  (supervisor)",    "",               "0x14000",  "ring 0",   "#b71c1c"),
        (288, 44, "#ef6c00", "#bf360c", "Kernel stack  (grows ↓)",   "",               "0x90000",  "ring 0",   "#b71c1c"),
        (340, 44, "#6d4c41", "#4e342e", "VGA graphics  (Mode 13h)",  "0xA0000",        "0xA0000",  "ring 0+3", "#1b5e20"),
        (392, 44, "#6d4c41", "#4e342e", "VGA text framebuffer",      "0xB8000",        "0xB8000",  "ring 0+3", "#1b5e20"),
    ]
    for (y, h, fill, outline, title, subtitle, addr, ring_txt, ring_color) in rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, subtitle, fb, fs)
        addr_label(y + h//2, addr)
        ring_label(y + h//2 - 4, ring_txt, ring_color)

    # ── shell process section ─────────────────────────────────────────────────
    SY = 460
    section_header(SY,
        "shell process  (page_dir_shell)  ·  virtual 0x400000 → physical 0x800000",
        "#2e7d32")

    shell_rows = [
        (SY+34, 52, "#43a047", "#2e7d32", "Shell code + data",   "virt 0x400000",    "0x800000"),
        (SY+96, 38, "#7b1fa2", "#4a148c", "Shell ARGS_BASE",     "virt 0x7FC000",    "0xBFC000"),
        (SY+144,38, "#f57c00", "#e65100", "Shell stack  (grows ↓)","virt 0x7FF000",  "0xBFF000"),
    ]
    for (y, h, fill, outline, title, subtitle, addr) in shell_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, subtitle, fb, fs)
        addr_label(y + h//2, addr)
        ring_label(y + h//2 - 4, "ring 3", "#1565c0")

    # ── child process section ─────────────────────────────────────────────────
    CY = 652
    section_header(CY,
        "child process  (page_dir_child)  ·  virtual 0x400000 → physical 0xC00000",
        "#1565c0")

    child_rows = [
        (CY+34, 52, "#1976d2", "#0d47a1", "Child code + data",  "virt 0x400000",    "0xC00000"),
        (CY+96, 38, "#7b1fa2", "#4a148c", "Child ARGS_BASE",    "virt 0x7FC000",    "0xFFC000"),
        (CY+144,38, "#f57c00", "#e65100", "Child stack  (grows ↓)","virt 0x7FF000", "0xFFF000"),
    ]
    for (y, h, fill, outline, title, subtitle, addr) in child_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, subtitle, fb, fs)
        addr_label(y + h//2, addr)
        ring_label(y + h//2 - 4, "ring 3", "#1565c0")

    text_center(draw, W // 2, 990, "(not to scale — gaps between regions omitted)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "memory-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    make_disk_layout()
    make_memory_layout()
