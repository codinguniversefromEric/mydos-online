#pragma once
// 讓 8259 PIC 的訊號能通過 Local APIC 送進 CPU（virtual wire 模式）
void apic_init(void);
