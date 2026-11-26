#include "irq.h"
#include "idt.h"
#include "pic.h"
#include "panic.h"
#include "process.h"

static irq_handler_t handlers[16];

// 所有 IRQ 都走這裡：呼叫驅動程式註冊的函式，再送 EOI。
// 忘了 EOI 的話，PIC 不會再送下一個中斷，鍵盤就只會動一次。
static void irq_dispatch(uint8_t irq, struct int_frame *f) {
    if (pic_is_spurious(irq)) return;
    if (handlers[irq]) handlers[irq]();
    pic_eoi(irq);
    // 中斷打斷的是使用者程式、而且按過 Ctrl+C：EOI 送完了，可以安全地結束程式
    process_check_break(f);
}

#define IRQ_STUB(n)                                                       \
    __attribute__((interrupt)) static void irq##n(struct int_frame *f) {  \
        irq_dispatch(n, f);                                               \
    }

IRQ_STUB(0)  IRQ_STUB(1)  IRQ_STUB(2)  IRQ_STUB(3)
IRQ_STUB(4)  IRQ_STUB(5)  IRQ_STUB(6)  IRQ_STUB(7)
IRQ_STUB(8)  IRQ_STUB(9)  IRQ_STUB(10) IRQ_STUB(11)
IRQ_STUB(12) IRQ_STUB(13) IRQ_STUB(14) IRQ_STUB(15)

static void *stubs[16] = {
    irq0, irq1, irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15,
};

void irq_init(void) {
    pic_init();
    for (int i = 0; i < 16; i++)
        idt_set_gate(PIC_IRQ_BASE + i, stubs[i]);
}

void irq_install(uint8_t irq, irq_handler_t handler) {
    handlers[irq] = handler;
    pic_unmask(irq);
}
