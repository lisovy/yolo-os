#!/usr/bin/env python3
"""Generate docs/disk-layout.png, docs/physical-layout.png, docs/virtual-layout.png."""

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

# ── physical-layout.png ───────────────────────────────────────────────────────

def make_physical_layout():
    W, H = 700, 570
    img = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb  = load_font(17)
    fs  = load_font_regular(14)
    ft  = load_font(24)
    fl  = load_font_regular(13)
    fh  = load_font(14)

    text_center(draw, W // 2, 34, "Physical memory layout", ft, "#222")

    LX   = 190
    RX   = 560
    LADDR = 178

    def addr_label(y, text):
        bbox = draw.textbbox((0, 0), text, font=fl)
        draw.text((LADDR - (bbox[2]-bbox[0]), y - (bbox[3]-bbox[1])//2), text, font=fl, fill="#555")

    def ring_label(y, text, color):
        draw.text((RX + 10, y - 8), text, font=fl, fill=color)

    def section_header(y, text, color):
        draw.line([(LX, y+2), (RX, y+2)], fill=color, width=2)
        text_center(draw, (LX+RX)//2, y + 18, text, fh, color)

    # ── kernel / hardware (static) ────────────────────────────────────────────
    section_header(62, "Kernel + hardware  (0x000000 – 0x0FFFFF, static)", "#424242")

    kern_rows = [
        (100, 38, "#9e9e9e", "#616161", "IVT / BIOS data",          "0x00000",  "ring 0",   "#b71c1c"),
        (144, 38, "#9e9e9e", "#616161", "MBR bootloader  (512 B)",   "0x07C00",  "ring 0",   "#b71c1c"),
        (188, 50, "#29b6f6", "#0277bd", "Kernel  (~16 KB)",          "0x10000",  "ring 0",   "#b71c1c"),
        (244, 34, "#e0e0e0", "#bdbdbd", "free RAM",                  "0x14000",  "ring 0",   "#b71c1c"),
        (284, 38, "#ef6c00", "#bf360c", "Kernel stack  (grows ↓)",   "0x90000",  "ring 0",   "#b71c1c"),
        (328, 34, "#6d4c41", "#4e342e", "VGA graphics  (Mode 13h)",  "0xA0000",  "ring 0+3", "#1b5e20"),
        (368, 34, "#6d4c41", "#4e342e", "VGA text framebuffer",      "0xB8000",  "ring 0+3", "#1b5e20"),
    ]
    for (y, h, fill, outline, title, addr, ring_txt, ring_color) in kern_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, "", fb, fs)
        addr_label(y + h//2, addr)
        ring_label(y + h//2 - 4, ring_txt, ring_color)

    # ── PMM dynamic region ────────────────────────────────────────────────────
    PM_Y = 424
    section_header(PM_Y - 8,
        "PMM dynamic  (0x100000 – 0x7FFFFFFF  ·  ~127 MB  ·  32 512 frames)",
        "#00695c")

    draw.rounded_rectangle([LX, PM_Y + 20, RX, PM_Y + 110],
                            radius=6, fill="#00897b", outline="#00695c", width=2)
    addr_label(PM_Y + 34, "0x100000")
    text_center(draw, (LX+RX)//2, PM_Y + 46,
                "bitmap PMM  ·  32 512 frames  ·  4 kB each", fh, "white")
    text_center(draw, (LX+RX)//2, PM_Y + 68,
                "~127 MB total  ·  up to ~430 procs", fl, "#e0f2f1")
    text_center(draw, (LX+RX)//2, PM_Y + 88,
                "see process-frames.png for detail", fl, "#b2dfdb")

    text_center(draw, W // 2, H - 18, "(heights not to scale)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "physical-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── virtual-layout.png ────────────────────────────────────────────────────────

def make_virtual_layout():
    W, H = 720, 430
    img = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb  = load_font(17)
    fs  = load_font_regular(14)
    ft  = load_font(20)
    fl  = load_font_regular(13)

    text_center(draw, W // 2, 30,
                "Virtual address space  (per process, all link at 0x400000)", ft, "#222")

    LX   = 210
    RX   = 530
    LADDR = 198

    def addr_label(y, text):
        bbox = draw.textbbox((0, 0), text, font=fl)
        draw.text((LADDR - (bbox[2]-bbox[0]), y - (bbox[3]-bbox[1])//2), text, font=fl, fill="#555")

    def ring_label(y, text, color):
        draw.text((RX + 10, y - 8), text, font=fl, fill=color)

    virt_rows = [
        (60,  52, "#9e9e9e", "#616161", "kernel  (supervisor only)",     "0x000000", "ring 0",   "#b71c1c"),
        (120, 54, "#43a047", "#2e7d32", "binary  256 kB",                "0x400000", "ring 3",   "#1565c0"),
        (182, 40, "#e8e8e8", "#bdbdbd", "unmapped  (~3.7 MB)",           "0x440000", "",         ""),
        (230, 36, "#7b1fa2", "#4a148c", "args  (ARGS_BASE)",             "0x7FC000", "ring 3",   "#1565c0"),
        (274, 48, "#f57c00", "#e65100", "stack  28 kB  (grows ↓)",       "0x7FF000", "ring 3",   "#1565c0"),
    ]
    for (y, h, fill, outline, title, addr, ring_txt, ring_color) in virt_rows:
        draw_block(draw, LX, y, RX, y+h, fill, outline, title, "", fb, fs)
        addr_label(y + h//2, addr)
        if ring_txt:
            ring_label(y + h//2 - 4, ring_txt, ring_color)

    # note at bottom
    text_center(draw, W // 2, 358,
                "per-process page tables map virtual 0x400000 → process-specific physical frames",
                fl, "#555")
    text_center(draw, W // 2, H - 18, "(heights not to scale)", fl, "#888")

    out = os.path.join(os.path.dirname(__file__), "virtual-layout.png")
    img.save(out)
    print(f"wrote {out}")

# ── interrupt-frame.png ───────────────────────────────────────────────────────
#
# Shows the 76-byte struct registers frame on phys_kstack after a ring-3 → ring-0
# transition.  Rows are ordered high-address (top) → low-address (bottom), so
# "stack grows downward" maps naturally onto the visual.

def make_interrupt_frame():
    ROW_H  = 46
    N_ROWS = 19
    LX, RX = 190, 530   # main column bounds
    OFF_X  = 86          # offset labels (left of LX)
    ANN_X  = RX + 12     # side annotation start

    TITLE_H = 78
    FOOTER  = 56
    W = 760
    H = TITLE_H + N_ROWS * ROW_H + FOOTER

    img  = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb = load_font(17)
    fs = load_font_regular(14)
    ft = load_font(20)
    fl = load_font_regular(13)
    fm = load_font_regular(13)

    # ── title ────────────────────────────────────────────────────────────────
    text_center(draw, W // 2, 28,
                "Interrupt / Syscall Stack Frame  (struct registers)", ft, "#222")
    text_center(draw, W // 2, 54,
                "on phys_kstack after ring-3 → ring-0 transition  ·  76 bytes", fl, "#555")

    # ── colour palette ───────────────────────────────────────────────────────
    C_CPU  = ("#ef6c00", "#e65100")   # CPU pushed
    C_ISR  = ("#f9a825", "#f57f17")   # ISR stub
    C_PU   = ("#1565c0", "#0d47a1")   # PUSHA
    C_SEG  = ("#2e7d32", "#1b5e20")   # segment regs

    # ── rows: (offset, field_label, value_hint, fill, outline) ───────────────
    # high address at top → low address at bottom
    rows = [
        (72, "user SS",     "0x23  (ring-3 data)",        *C_CPU),
        (68, "user ESP",    "0x7FF000",                   *C_CPU),
        (64, "EFLAGS",      "0x3200  (IF=1, IOPL=3)",     *C_CPU),
        (60, "CS",          "0x1B  (ring-3 code)",        *C_CPU),
        (56, "EIP",         "0x400000  (entry point)",    *C_CPU),
        (52, "err_code",    "0  (or h/w error code)",     *C_ISR),
        (48, "int_no",      "32 / 33 / 128 / …",         *C_ISR),
        (44, "EAX",         "",  *C_PU),
        (40, "ECX",         "",  *C_PU),
        (36, "EDX",         "",  *C_PU),
        (32, "EBX",         "",  *C_PU),
        (28, "ESP",         "(ignored by popa)",          *C_PU),
        (24, "EBP",         "",  *C_PU),
        (20, "ESI",         "",  *C_PU),
        (16, "EDI",         "",  *C_PU),
        (12, "DS",          "0x23",  *C_SEG),
        ( 8, "ES",          "0x23",  *C_SEG),
        ( 4, "FS",          "0x23",  *C_SEG),
        ( 0, "GS",          "0x23",  *C_SEG),
    ]

    # ── draw rows ────────────────────────────────────────────────────────────
    for idx, (off, field, hint, fill, outline) in enumerate(rows):
        y = TITLE_H + idx * ROW_H
        # box
        draw.rectangle([LX, y, RX, y + ROW_H - 1], fill=fill, outline=outline)
        # offset label on left
        off_txt = f"+{off}"
        bbox = draw.textbbox((0, 0), off_txt, font=fm)
        draw.text((OFF_X - (bbox[2] - bbox[0]) - 4, y + (ROW_H - (bbox[3]-bbox[1]))//2),
                  off_txt, font=fm, fill="#555")
        # field name
        if hint:
            text_center(draw, (LX + RX)//2,
                        y + ROW_H//2 - 7, field, fb, "white")
            text_center(draw, (LX + RX)//2,
                        y + ROW_H//2 + 8, hint, fl, "#ffe0b2")
        else:
            text_center(draw, (LX + RX)//2, y + ROW_H//2, field, fb, "white")

    # ── side brackets ────────────────────────────────────────────────────────
    def bracket(row_start, row_end, label, color):
        y0 = TITLE_H + row_start * ROW_H + 2
        y1 = TITLE_H + row_end   * ROW_H - 2
        bx = RX + 6
        draw.line([(bx, y0), (bx, y1)], fill=color, width=2)
        draw.line([(bx, y0), (bx+5, y0)], fill=color, width=2)
        draw.line([(bx, y1), (bx+5, y1)], fill=color, width=2)
        cy = (y0 + y1) // 2
        draw.text((bx + 10, cy - 8), label, font=fl, fill=color)

    bracket( 0,  5, "CPU pushes\n(privilege change)", C_CPU[0])
    bracket( 5,  7, "ISR stub",                       C_ISR[0])
    bracket( 7, 15, "pusha",                           C_PU[0])
    bracket(15, 19, "push seg regs",                   C_SEG[0])

    # ── tss.esp0 marker (top of kstack, above first row) ─────────────────────
    tss_y = TITLE_H - 6
    draw.line([(LX, tss_y), (RX, tss_y)], fill="#6a1b9a", width=2)
    draw.text((LX + 4, tss_y - 16), "tss.esp0  (kstack top — unused space above)", font=fl, fill="#6a1b9a")

    # ── saved_esp marker (bottom of last row) ────────────────────────────────
    bot_y = TITLE_H + N_ROWS * ROW_H
    draw.line([(LX, bot_y), (RX, bot_y)], fill="#c62828", width=2)
    arrow_txt = "saved_esp  (= phys_kstack + 4096 − 76)"
    text_center(draw, (LX + RX)//2, bot_y + 18, arrow_txt, fl, "#c62828")

    # ── legend ───────────────────────────────────────────────────────────────
    legend = [
        (C_CPU[0], "CPU  (ring-3 → ring-0)"),
        (C_ISR[0], "ISR stub  (int_no / err_code)"),
        (C_PU[0],  "pusha  (GP registers)"),
        (C_SEG[0], "push DS/ES/FS/GS"),
    ]
    lx0 = 20
    for i, (col, lbl) in enumerate(legend):
        lx = lx0 + i * 185
        draw.rectangle([lx, H - 28, lx + 14, H - 14], fill=col)
        draw.text((lx + 18, H - 28), lbl, font=fl, fill="#333")

    out = os.path.join(os.path.dirname(__file__), "interrupt-frame.png")
    img.save(out)
    print(f"wrote {out}")


# ── process-frames.png ────────────────────────────────────────────────────────
#
# Per-process physical frame allocation.  Three tracked PMM pointers
# (phys_frames[0], phys_frames[1], phys_kstack) plus the binary and stack pages
# tracked implicitly via the page table.

def make_process_frames():
    W      = 700
    Y0     = 78
    FOOTER = 80

    fb = load_font(17)
    fs = load_font_regular(14)
    ft = load_font(22)
    fl = load_font_regular(13)

    # Two-column table: left = bar, right = annotations
    BAR_L, BAR_R = 180, 380

    # Rows: (label, size_label, kb, fill, outline, pointer_label)
    # Heights are proportional to frame counts (not exact — binary is dominant)
    rows = [
        ("page directory",   "1 frame  =    4 kB", 4,   "#00695c", "#004d40", "phys_frames[0]"),
        ("user page table",  "1 frame  =    4 kB", 4,   "#00897b", "#00695c", "phys_frames[1]"),
        ("kernel stack",     "1 frame  =    4 kB", 4,   "#6a1b9a", "#4a148c", "phys_kstack"),
        ("user binary",      "64 frames = 256 kB", 256, "#1565c0", "#0d47a1", "PT scan [0..63]"),
        ("user stack + args","7 frames  =  28 kB", 28,  "#e65100", "#bf360c", "PT scan [1016..1022]"),
    ]
    total_kb = sum(r[2] for r in rows)
    MIN_ROW  = 46
    AVAIL_H  = 540   # target total bar height (will stretch up if min-rows dominate)

    # Pre-compute row heights so we know exact total height before creating the image
    row_heights = [max(MIN_ROW, int(AVAIL_H * r[2] / total_kb)) for r in rows]
    bar_h = sum(row_heights)
    H = Y0 + bar_h + FOOTER

    img  = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    text_center(draw, W // 2, 30, "Per-process Physical Frame Allocation", ft, "#222")
    text_center(draw, W // 2, 56, "~74 frames × 4 kB = 296 kB per process", fl, "#555")

    Y0 = 78
    cur_y = Y0
    for (label, size_lbl, kb, fill, outline, ptr), rh in zip(rows, row_heights):
        rh = rh  # already computed
        draw.rounded_rectangle([BAR_L, cur_y, BAR_R, cur_y + rh],
                                radius=4, fill=fill, outline=outline, width=2)
        cy = cur_y + rh // 2
        text_center(draw, (BAR_L + BAR_R)//2, cy - 7, label, fb, "white")
        text_center(draw, (BAR_L + BAR_R)//2, cy + 9, size_lbl, fl, "#e0e0e0")

        # Right annotation: pointer name
        draw.line([(BAR_R + 2, cy), (BAR_R + 26, cy)], fill="#555", width=1)
        draw.text((BAR_R + 30, cy - 8), ptr, font=fl, fill="#333")

        cur_y += rh

    # Total bar outline
    draw.rounded_rectangle([BAR_L - 2, Y0 - 2, BAR_R + 2, cur_y + 2],
                            radius=6, outline="#333", width=2, fill=None)

    # Left address / size annotation
    draw.text((10, Y0), f"top", font=fl, fill="#888")
    draw.text((10, cur_y - 12), f"({total_kb} kB)", font=fl, fill="#888")

    # Note below
    text_center(draw, W // 2, cur_y + 30,
                "binary + stack freed by scanning phys_frames[1] (PT) on process exit",
                fl, "#555")
    text_center(draw, W // 2, cur_y + 50,
                "phys_frames[0] (PD) and phys_kstack freed explicitly",
                fl, "#555")

    out = os.path.join(os.path.dirname(__file__), "process-frames.png")
    img.save(out)
    print(f"wrote {out}")


# ── page-directory.png ────────────────────────────────────────────────────────
#
# Shows the 1024-entry page directory split into three regions, the shared
# pt_kernel, and the per-process user PT with its VPN groups.

def make_page_directory():
    W, H = 760, 580
    img  = Image.new("RGB", (W, H), "#f5f5f5")
    draw = ImageDraw.Draw(img)

    fb = load_font(15)
    fs = load_font_regular(13)
    ft = load_font(21)
    fl = load_font_regular(12)

    text_center(draw, W // 2, 28, "Page Directory Layout  (per process)", ft, "#222")

    # ── Column layout ────────────────────────────────────────────────────────
    # Left panel: Page Directory, Center: pt_kernel, Right: per-process PT
    PD_L,  PD_R  = 14,  248
    PKL_L, PKL_R = 274, 494
    UPT_L, UPT_R = 520, 748

    def panel_title(x1, x2, y, text, color):
        text_center(draw, (x1+x2)//2, y, text, fb, color)

    def entry(x1, x2, y, h, fill, outline, line1, line2=""):
        draw.rounded_rectangle([x1, y, x2, y+h], radius=4,
                                fill=fill, outline=outline, width=2)
        cy = y + h//2
        if line2:
            text_center(draw, (x1+x2)//2, cy - 8, line1, fs, "white")
            text_center(draw, (x1+x2)//2, cy + 8, line2, fl, "#e0e0e0")
        else:
            text_center(draw, (x1+x2)//2, cy, line1, fs, "white")

    # ── Page directory ───────────────────────────────────────────────────────
    Y0 = 58
    panel_title(PD_L, PD_R, Y0, "Page Directory (CR3)", "#222")
    panel_title(PD_L, PD_R, Y0+18, "1024 entries × 4 B = 4 kB", "#555")

    PD_Y = Y0 + 36
    entry(PD_L, PD_R, PD_Y,      46, "#2e7d32", "#1b5e20",
          "PDE[0] → pt_kernel", "kernel 0–4 MB  ring 0")
    entry(PD_L, PD_R, PD_Y+ 50,  46, "#1565c0", "#0d47a1",
          "PDE[1] → per-process PT", "user 0–4 MB  ring 0+3")
    entry(PD_L, PD_R, PD_Y+100, 110, "#546e7a", "#37474f",
          "PDE[2 – 511]", "4 MB PSE  ·  identity  ·  ring 0")
    text_center(draw, (PD_L+PD_R)//2, PD_Y+174,
                "supervisor-only identity access", fl, "#e0e0e0")
    text_center(draw, (PD_L+PD_R)//2, PD_Y+190,
                "to all physical RAM", fl, "#e0e0e0")
    entry(PD_L, PD_R, PD_Y+214,  50, "#9e9e9e", "#757575",
          "PDE[512 – 1023]", "0  (unused)")

    # ── pt_kernel (shared) ───────────────────────────────────────────────────
    panel_title(PKL_L, PKL_R, Y0, "pt_kernel  (static, shared)", "#2e7d32")
    panel_title(PKL_L, PKL_R, Y0+18, "1024 entries  ·  maps 0–4 MB", "#555")

    PT_Y = Y0 + 36
    entry(PKL_L, PKL_R, PT_Y,     50, "#388e3c", "#1b5e20",
          "PTE[0..159]", "0x00000–0x09FFFF  ring 0")
    entry(PKL_L, PKL_R, PT_Y+ 54, 36, "#558b2f", "#33691e",
          "PTE[160..191]", "VGA  ·  ring 0+3")
    entry(PKL_L, PKL_R, PT_Y+ 94, 36, "#9e9e9e", "#757575",
          "PTE[192..1023]", "not mapped  (0)")

    text_center(draw, (PKL_L+PKL_R)//2, PT_Y+148,
                "shared: all processes map", fl, "#555")
    text_center(draw, (PKL_L+PKL_R)//2, PT_Y+164,
                "PDE[0] → this table", fl, "#555")

    # ── per-process user PT ──────────────────────────────────────────────────
    panel_title(UPT_L, UPT_R, Y0, "User PT  (per process)", "#1565c0")
    panel_title(UPT_L, UPT_R, Y0+18, "1024 entries  ·  maps 0–4 MB", "#555")

    UP_Y = Y0 + 36
    entry(UPT_L, UPT_R, UP_Y,      52, "#1565c0", "#0d47a1",
          "PTE[0..63]", "binary  256 kB  ring 3")
    entry(UPT_L, UPT_R, UP_Y+  56, 52, "#e8e8e8", "#9e9e9e",
          "PTE[64..1015]", "not mapped  (0)")
    entry(UPT_L, UPT_R, UP_Y+ 112, 34, "#7b1fa2", "#4a148c",
          "PTE[1016..1019]", "stack  ring 3")
    entry(UPT_L, UPT_R, UP_Y+ 150, 22, "#6a1b9a", "#4a148c",
          "PTE[1020]  args", "")
    entry(UPT_L, UPT_R, UP_Y+ 176, 22, "#7b1fa2", "#4a148c",
          "PTE[1021..1022]  stack", "")
    entry(UPT_L, UPT_R, UP_Y+ 202, 24, "#9e9e9e", "#757575",
          "PTE[1023]  (0)", "")

    # ── connecting arrows ────────────────────────────────────────────────────
    # PDE[0] → pt_kernel
    ay = PD_Y + 23
    draw.line([(PD_R, ay), (PKL_L, ay)], fill="#2e7d32", width=2)
    draw.polygon([(PKL_L, ay), (PKL_L-8, ay-5), (PKL_L-8, ay+5)], fill="#2e7d32")

    # PDE[1] → per-process PT
    ay2 = PD_Y + 73
    draw.line([(PD_R, ay2), (UPT_L, ay2)], fill="#1565c0", width=2)
    draw.polygon([(UPT_L, ay2), (UPT_L-8, ay2-5), (UPT_L-8, ay2+5)], fill="#1565c0")

    # ── note at bottom ───────────────────────────────────────────────────────
    note_y = H - 76
    draw.line([(10, note_y), (W-10, note_y)], fill="#ccc", width=1)
    text_center(draw, W//2, note_y+16,
                "PDE[2–511]: 4 MB PSE identity pages give the kernel write access to any PMM frame",
                fl, "#555")
    text_center(draw, W//2, note_y+32,
                "without per-process kernel mappings.  U/S=0 (ring 0 only).",
                fl, "#555")
    text_center(draw, W//2, note_y+52,
                "pt_kernel is shared: all processes point PDE[0] to the same physical table.",
                fl, "#555")

    out = os.path.join(os.path.dirname(__file__), "page-directory.png")
    img.save(out)
    print(f"wrote {out}")


# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    make_disk_layout()
    make_physical_layout()
    make_virtual_layout()
    make_interrupt_frame()
    make_process_frames()
    make_page_directory()
