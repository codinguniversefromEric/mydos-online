#include "idt.h"
#include "panic.h"

#define KERNEL_CS 0x08   // 3a 自己的 GDT：第 1 項 kernel code

// 一個 IDT 項目 16 byte，處理函式的位址被拆成三段
struct __attribute__((packed)) idt_entry {
    uint16_t offset_lo;    // 位址 bit 0–15
    uint16_t selector;     // 程式碼段：GDT 的 kernel code (0x08)
    uint8_t  ist;          // 0 = 用目前的堆疊
    uint8_t  type_attr;    // 0x8E = 存在、ring 0、64 位元中斷閘
    uint16_t offset_mid;   // 位址 bit 16–31
    uint32_t offset_hi;    // 位址 bit 32–63
    uint32_t zero;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];   // 不能加 const，理由和 GDT 一樣

void idt_set_gate(int vec, void *handler) { idt_set_gate_ex(vec, handler, 0, 0); }

void idt_set_gate_ex(int vec, void *handler, int dpl, int ist) {
    uint64_t a = (uint64_t)handler;
    idt[vec].offset_lo  = a & 0xFFFF;
    idt[vec].selector   = KERNEL_CS;
    idt[vec].ist        = ist;
    idt[vec].type_attr  = 0x8E | (dpl << 5);   // bit 5–6 = DPL
    idt[vec].offset_mid = (a >> 16) & 0xFFFF;
    idt[vec].offset_hi  = (uint32_t)(a >> 32);
    idt[vec].zero       = 0;
}

// 每個向量需要自己的函式，CPU 才知道是幾號例外。
// 有些例外 CPU 會多推一個錯誤碼，函式就要多收一個參數。
#define EXC(n)                                                            \
    __attribute__((interrupt)) static void exc##n(struct int_frame *f) {  \
        panic_exception(n, 0, 0, f);                                      \
    }
#define EXC_ERR(n)                                                        \
    __attribute__((interrupt)) static void exc##n(struct int_frame *f,    \
                                                  unsigned long err) {   \
        panic_exception(n, err, 1, f);                                    \
    }

EXC(0)  EXC(1)  EXC(2)  EXC(3)  EXC(4)  EXC(5)  EXC(6)  EXC(7)
EXC_ERR(8)  EXC(9)  EXC_ERR(10) EXC_ERR(11) EXC_ERR(12) EXC_ERR(13) EXC_ERR(14) EXC(15)
EXC(16) EXC_ERR(17) EXC(18) EXC(19) EXC(20) EXC_ERR(21) EXC(22) EXC(23)
EXC(24) EXC(25) EXC(26) EXC(27) EXC(28) EXC_ERR(29) EXC_ERR(30) EXC(31)

static void *exc_table[32] = {
    exc0,  exc1,  exc2,  exc3,  exc4,  exc5,  exc6,  exc7,
    exc8,  exc9,  exc10, exc11, exc12, exc13, exc14, exc15,
    exc16, exc17, exc18, exc19, exc20, exc21, exc22, exc23,
    exc24, exc25, exc26, exc27, exc28, exc29, exc30, exc31,
};

void idt_init(void) {
    for (int i = 0; i < 32; i++)
        idt_set_gate(i, exc_table[i]);
    // Double Fault 通常代表堆疊壞了，換到 TSS 準備的乾淨堆疊（IST1）再處理
    idt_set_gate_ex(8, exc_table[8], 0, 1);
    struct idtr r = { sizeof(idt) - 1, (uint64_t)idt };
    __asm__ volatile ("lidt %0" :: "m"(r) : "memory");
}
