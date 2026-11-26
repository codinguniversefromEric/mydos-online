#include "panic.h"
#include "term.h"
#include "kprintf.h"
#include "process.h"

static const char *exc_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range",
    "Invalid Opcode", "Device Not Available", "Double Fault", "Coprocessor Overrun",
    "Invalid TSS", "Segment Not Present", "Stack Fault", "General Protection",
    "Page Fault", "Reserved", "x87 FPU Error", "Alignment Check", "Machine Check",
    "SIMD FP Error", "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor Injection",
    "VMM Communication", "Security", "Reserved",
};

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static void bluescreen(void) {
    __asm__ volatile ("cli");                 // 當機時不再接受任何中斷
    term_set_color(0xFFFFFF, 0x0000AA);       // Win95 藍底白字
    term_clear();
    kprintf("\n  *** MYDOS STOP ***\n\n");
}

void panic(const char *fmt, ...) {
    bluescreen();
    kprintf("  ");
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n\n  System halted.\n");
    halt_forever();
    __builtin_unreachable();
}

void panic_exception(int vec, uint64_t err, int has_err, struct int_frame *f) {
    // 出事的是 ring 3 的程式：只結束那支程式，系統照常運作
    if ((f->cs & 3) == 3 && process_running())
        process_fault(vec, exc_names[vec], err, f);
    bluescreen();
    kprintf("  CPU exception %d: %s\n\n", vec, exc_names[vec]);
    if (has_err)
        kprintf("  ERROR   %016llx\n", err);
    kprintf("  RIP     %016llx\n", f->rip);
    kprintf("  CS      %016llx\n", f->cs);
    kprintf("  RFLAGS  %016llx\n", f->rflags);
    kprintf("  RSP     %016llx\n", f->rsp);
    kprintf("  SS      %016llx\n", f->ss);
    if (vec == 14) {                          // Page Fault：CR2 是出事的位址
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("  CR2     %016llx\n", cr2);
    }
    kprintf("\n  System halted.\n");
    halt_forever();
    __builtin_unreachable();
}
