/*
 * idt.c - IDT setup and PIC 8259 remapping
 *
 * Sets up 256-entry IDT, remaps the 8259 PIC so that hardware IRQs
 * land at INT 32-47 (not 8-15 where they would collide with CPU exceptions),
 * and installs gates for exceptions (0-31), IRQs (32-47) and syscall (128).
 */

/* ============================================================
 * Local I/O helper (outb — mirrors the one in kernel.c)
 * ============================================================ */

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
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
    /* Mask all IRQs — we do not use interrupt-driven I/O yet */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
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
