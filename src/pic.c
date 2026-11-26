#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20   // 主 PIC：IRQ 0–7
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0   // 從 PIC：IRQ 8–15，接在主 PIC 的 IRQ2
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

// 老 PIC 反應慢，每次寫完等一下：寫 0x80 這個沒人用的 port 約耗 1 微秒
static inline void io_wait(void) { outb(0x80, 0); }

// 開機時 PIC 把 IRQ 0–7 放在向量 8–15，剛好和 CPU 例外撞在一起
// （例如鍵盤 IRQ1 會被當成 Double Fault）。所以要搬到 32 以後。
void pic_init(void) {
    outb(PIC1_CMD, 0x11);  io_wait();   // ICW1：開始初始化，之後會給 ICW4
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, PIC_IRQ_BASE);     io_wait();   // ICW2：主 PIC 從向量 32 開始
    outb(PIC2_DATA, PIC_IRQ_BASE + 8); io_wait();   //       從 PIC 從向量 40 開始
    outb(PIC1_DATA, 0x04); io_wait();   // ICW3：從 PIC 接在主 PIC 的 IRQ2（bit 2）
    outb(PIC2_DATA, 0x02); io_wait();   //       從 PIC 的身分編號是 2
    outb(PIC1_DATA, 0x01); io_wait();   // ICW4：8086 模式
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);   // 全部遮蔽，需要哪條再打開
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
        irq = 2;                         // 從 PIC 的訊號要經過主 PIC 的 IRQ2
    }
    outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
}

void pic_mask(uint8_t irq) {
    if (irq >= 8) outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
    else          outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);   // 從 PIC 來的要兩邊都通知
    outb(PIC1_CMD, PIC_EOI);
}

// IRQ7 / IRQ15 可能是雜訊造成的「假中斷」：讀 ISR 暫存器確認
int pic_is_spurious(uint8_t irq) {
    if (irq != 7 && irq != 15) return 0;
    uint16_t cmd = (irq == 7) ? PIC1_CMD : PIC2_CMD;
    outb(cmd, 0x0B);                         // 下次讀取回傳 ISR
    if (inb(cmd) & 0x80) return 0;           // bit 7 有亮 → 真的中斷
    if (irq == 15) outb(PIC1_CMD, PIC_EOI);  // 假的：只通知主 PIC
    return 1;
}
