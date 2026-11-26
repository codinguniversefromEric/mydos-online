#pragma once
#include <stdint.h>

typedef void (*irq_handler_t)(void);

void irq_init(void);                                 // 填 IDT 32–47、初始化 PIC
void irq_install(uint8_t irq, irq_handler_t handler); // 註冊並打開該 IRQ

static inline void irq_enable(void)  { __asm__ volatile ("sti"); }
static inline void irq_disable(void) { __asm__ volatile ("cli"); }
