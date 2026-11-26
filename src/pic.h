#pragma once
#include <stdint.h>

#define PIC_IRQ_BASE 32           // IRQ 0–15 對應到向量 32–47

void pic_init(void);              // 重新對應，並先遮蔽全部 IRQ
void pic_unmask(uint8_t irq);     // 打開某條 IRQ
void pic_mask(uint8_t irq);
void pic_eoi(uint8_t irq);        // 告訴 PIC「這個中斷處理完了」
int  pic_is_spurious(uint8_t irq);
