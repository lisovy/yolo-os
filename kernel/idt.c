/*
 * idt.c - IDT setup, GDT + TSS, and PIC 8259 remapping
 *
 * Sets up 256-entry IDT, remaps the 8259 PIC so that hardware IRQs
 * land at INT 32-47 (not 8-15 where they would collide with CPU exceptions),
 * installs gates for exceptions (0-31), IRQs (32-47) and syscall (128).
 * Also sets up a 6-entry GDT with ring-0/ring-3 segments and a TSS.
 */

/* ============================================================
 * Local I/O helper (outb — mirrors the one in kernel.c)
 * ============================================================ */

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ============================================================
 * GDT — 6 descriptors
 * ============================================================ */

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;

struct gdt_entry {
    uint16_t limit_low;    /* limit bits 0-15     */
    uint16_t base_low;     /* base  bits 0-15     */
    uint8_t  base_mid;     /* base  bits 16-23    */
    uint8_t  access;       /* type + DPL + P      */
    uint8_t  granularity;  /* limit bits 16-19 + flags */
    uint8_t  base_high;    /* base  bits 24-31    */
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* TSS — only the fields used by the CPU for ring-0 stack on privilege change */
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;        /* ring-0 stack pointer used on INT from ring 3 */
    uint16_t ss0;         /* ring-0 stack segment                         */
    uint16_t pad0;
    /* remaining 88 bytes not used — zeroed */
    uint32_t unused[22];
    uint16_t iopb_offset; /* points past TSS end → no I/O permission bitmap */
    uint16_t pad1;
} __attribute__((packed)) tss_t;

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdtp;
static tss_t            tss;
uint8_t                 tss_stack[4096];  /* kernel stack for ISR when coming from ring 3 */

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    gdt[i].base_low   = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid   = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high  = (uint8_t)((base >> 24) & 0xFF);
    gdt[i].limit_low  = (uint16_t)(limit & 0xFFFF);
    gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access     = access;
}

void gdt_init(void)
{
    /* [0x00] null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);
    /* [0x08] ring-0 code: base=0, limit=4GB, DPL=0, 32-bit, code R/X */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);
    /* [0x10] ring-0 data: base=0, limit=4GB, DPL=0, 32-bit, data R/W */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);
    /* [0x18] ring-3 code: base=0, limit=4GB, DPL=3, 32-bit, code R/X */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);
    /* [0x20] ring-3 data: base=0, limit=4GB, DPL=3, 32-bit, data R/W */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);
    /* [0x28] TSS descriptor: type=0x89 (32-bit available TSS), DPL=0 */
    {
        uint32_t tss_base  = (uint32_t)&tss;
        uint32_t tss_limit = sizeof(tss) - 1;
        gdt[5].base_low   = (uint16_t)(tss_base & 0xFFFF);
        gdt[5].base_mid   = (uint8_t)((tss_base >> 16) & 0xFF);
        gdt[5].base_high  = (uint8_t)((tss_base >> 24) & 0xFF);
        gdt[5].limit_low  = (uint16_t)(tss_limit & 0xFFFF);
        gdt[5].granularity = (uint8_t)((tss_limit >> 16) & 0x0F);
        gdt[5].access     = 0x89;  /* present, DPL=0, type=9 (32-bit TSS available) */
    }

    /* Zero TSS, then configure */
    {
        uint8_t *p = (uint8_t *)&tss;
        for (unsigned int k = 0; k < sizeof(tss); k++) p[k] = 0;
    }
    tss.ss0         = 0x10;          /* ring-0 data selector */
    tss.esp0        = (uint32_t)tss_stack + sizeof(tss_stack);
    tss.iopb_offset = (uint16_t)sizeof(tss);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    __asm__ volatile (
        "lgdt %0\n"
        /* Reload segment registers to use new GDT.
         * CS is reloaded via a far jump; data segments via mov. */
        "ljmp $0x08, $1f\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "m"(gdtp) : "eax"
    );

    /* Load Task Register */
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)0x28));
}

void tss_set_ring0_stack(uint32_t esp)
{
    tss.esp0 = esp;
}


/* ============================================================
 * IDT structures
 * ============================================================ */

struct idt_entry {
    unsigned short offset_low;   /* handler address bits 0-15  */
    unsigned short selector;     /* code segment selector       */
    unsigned char  zero;         /* always 0                    */
    unsigned char  type_attr;    /* gate type + DPL + present   */
    unsigned short offset_high;  /* handler address bits 16-31  */
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

/*
 * type_attr values:
 *   0x8E — 32-bit interrupt gate, DPL=0 (kernel only)
 *   0xEE — 32-bit interrupt gate, DPL=3 (callable from ring 3 too)
 */
static void idt_set_gate(int n, unsigned int handler, unsigned char type_attr)
{
    idt[n].offset_low  = (unsigned short)(handler & 0xFFFF);
    idt[n].selector    = 0x08;   /* kernel code segment */
    idt[n].zero        = 0;
    idt[n].type_attr   = type_attr;
    idt[n].offset_high = (unsigned short)(handler >> 16);
}

/* ============================================================
 * PIC 8259 remapping
 *
 * Default mapping: IRQ 0-7  → INT  8-15  (clashes with CPU exceptions)
 *                  IRQ 8-15 → INT 112-119
 * After remap:     IRQ 0-7  → INT 32-39
 *                  IRQ 8-15 → INT 40-47
 * ============================================================ */

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void pic_remap(void)
{
    /* ICW1: begin initialisation */
    outb(PIC1_CMD,  0x11);
    outb(PIC2_CMD,  0x11);
    /* ICW2: vector offsets */
    outb(PIC1_DATA, 0x20);   /* master: IRQ 0 → INT 32 */
    outb(PIC2_DATA, 0x28);   /* slave:  IRQ 8 → INT 40 */
    /* ICW3: cascade wiring */
    outb(PIC1_DATA, 0x04);   /* master: slave attached to IRQ2 */
    outb(PIC2_DATA, 0x02);   /* slave:  cascade identity = 2   */
    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    /* Mask all IRQs initially; pit_init() will unmask IRQ0 */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/* ============================================================
 * PIT (Programmable Interval Timer) — channel 0 at 100 Hz
 *
 * Oscillator: 1 193 180 Hz.  Divisor = 1193180 / 100 = 11931 → ~100.02 Hz.
 * Must match PIT_HZ defined in kernel.c.
 * ============================================================ */

#define PIT_CMD  0x43
#define PIT_CH0  0x40

void pit_init(void)
{
    unsigned int divisor = 11932;          /* 1193180 / 100 Hz */
    outb(PIT_CMD, 0x36);                   /* channel 0, lo/hi byte, mode 3 (square wave) */
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)(divisor >> 8));
    /* Unmask IRQ0 (PIT) and IRQ1 (PS/2 keyboard) in master PIC */
    outb(PIC1_DATA, 0xFC);
}

/* ============================================================
 * ISR stubs declared in isr.asm
 * ============================================================ */

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void isr32(void); extern void isr33(void); extern void isr34(void);
extern void isr35(void); extern void isr36(void); extern void isr37(void);
extern void isr38(void); extern void isr39(void); extern void isr40(void);
extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void);
extern void isr47(void);

extern void isr128(void);

/* ============================================================
 * Public init function
 * ============================================================ */

void idt_init(void)
{
    pic_remap();

    /* CPU exceptions (0-31): ring-0 interrupt gates */
    idt_set_gate( 0, (unsigned int)isr0,  0x8E);
    idt_set_gate( 1, (unsigned int)isr1,  0x8E);
    idt_set_gate( 2, (unsigned int)isr2,  0x8E);
    idt_set_gate( 3, (unsigned int)isr3,  0x8E);
    idt_set_gate( 4, (unsigned int)isr4,  0x8E);
    idt_set_gate( 5, (unsigned int)isr5,  0x8E);
    idt_set_gate( 6, (unsigned int)isr6,  0x8E);
    idt_set_gate( 7, (unsigned int)isr7,  0x8E);
    idt_set_gate( 8, (unsigned int)isr8,  0x8E);
    idt_set_gate( 9, (unsigned int)isr9,  0x8E);
    idt_set_gate(10, (unsigned int)isr10, 0x8E);
    idt_set_gate(11, (unsigned int)isr11, 0x8E);
    idt_set_gate(12, (unsigned int)isr12, 0x8E);
    idt_set_gate(13, (unsigned int)isr13, 0x8E);
    idt_set_gate(14, (unsigned int)isr14, 0x8E);
    idt_set_gate(15, (unsigned int)isr15, 0x8E);
    idt_set_gate(16, (unsigned int)isr16, 0x8E);
    idt_set_gate(17, (unsigned int)isr17, 0x8E);
    idt_set_gate(18, (unsigned int)isr18, 0x8E);
    idt_set_gate(19, (unsigned int)isr19, 0x8E);
    idt_set_gate(20, (unsigned int)isr20, 0x8E);
    idt_set_gate(21, (unsigned int)isr21, 0x8E);
    idt_set_gate(22, (unsigned int)isr22, 0x8E);
    idt_set_gate(23, (unsigned int)isr23, 0x8E);
    idt_set_gate(24, (unsigned int)isr24, 0x8E);
    idt_set_gate(25, (unsigned int)isr25, 0x8E);
    idt_set_gate(26, (unsigned int)isr26, 0x8E);
    idt_set_gate(27, (unsigned int)isr27, 0x8E);
    idt_set_gate(28, (unsigned int)isr28, 0x8E);
    idt_set_gate(29, (unsigned int)isr29, 0x8E);
    idt_set_gate(30, (unsigned int)isr30, 0x8E);
    idt_set_gate(31, (unsigned int)isr31, 0x8E);

    /* Hardware IRQs (32-47): ring-0 interrupt gates */
    idt_set_gate(32, (unsigned int)isr32, 0x8E);
    idt_set_gate(33, (unsigned int)isr33, 0x8E);
    idt_set_gate(34, (unsigned int)isr34, 0x8E);
    idt_set_gate(35, (unsigned int)isr35, 0x8E);
    idt_set_gate(36, (unsigned int)isr36, 0x8E);
    idt_set_gate(37, (unsigned int)isr37, 0x8E);
    idt_set_gate(38, (unsigned int)isr38, 0x8E);
    idt_set_gate(39, (unsigned int)isr39, 0x8E);
    idt_set_gate(40, (unsigned int)isr40, 0x8E);
    idt_set_gate(41, (unsigned int)isr41, 0x8E);
    idt_set_gate(42, (unsigned int)isr42, 0x8E);
    idt_set_gate(43, (unsigned int)isr43, 0x8E);
    idt_set_gate(44, (unsigned int)isr44, 0x8E);
    idt_set_gate(45, (unsigned int)isr45, 0x8E);
    idt_set_gate(46, (unsigned int)isr46, 0x8E);
    idt_set_gate(47, (unsigned int)isr47, 0x8E);

    /* Syscall (int 0x80): DPL=3 so user-mode code can call it later */
    idt_set_gate(128, (unsigned int)isr128, 0xEE);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (unsigned int)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtp));
}
