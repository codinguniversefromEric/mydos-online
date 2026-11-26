#pragma once
#include <stdint.h>

void idt_init(void);                        // 建表、填 32 個例外、lidt
void idt_set_gate(int vec, void *handler);  // 讓 irq.c 填 32–47
// 進階版：dpl = 3 代表使用者程式可以用 int 指令觸發；ist ≠ 0 代表換到緊急堆疊
void idt_set_gate_ex(int vec, void *handler, int dpl, int ist);
