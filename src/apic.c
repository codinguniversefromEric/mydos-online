#include "apic.h"
#include "mm.h"
#include "idt.h"
#include "panic.h"
#include <stdint.h>

// 新的 CPU 每顆核心都有一個 Local APIC，PIC 的輸出接在它的 LINT0 腳上。
// 開機程式（例如 Limine v11）可能把 LINT0 遮蔽掉，結果是：
// PIC 收到了按鍵（IRR 有亮），CPU 卻永遠等不到中斷。
// 這裡把 LINT0 設成 ExtINT：「把 PIC 的中斷原封不動轉給 CPU」。

#define LAPIC_SVR   0x0F0   // Spurious Vector Register，bit 8 = 啟用 APIC
#define LAPIC_LVT0  0x350   // LINT0：接 PIC
#define LAPIC_LVT1  0x360   // LINT1：接 NMI
#define LVT_EXTINT  (7 << 8)
#define LVT_NMI     (4 << 8)
#define SPURIOUS_VEC 0xFF

static volatile uint32_t *lapic;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static void lapic_write(uint32_t reg, uint32_t v) { lapic[reg / 4] = v; }

// APIC 在中斷「被撤回」時會送這個向量，不需要 EOI，直接返回就好
__attribute__((interrupt)) static void spurious(struct int_frame *f) { (void)f; }

void apic_init(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1 << 9))) return;            // 沒有 APIC 的老 CPU：PIC 直通，什麼都不用做

    uint64_t base = rdmsr(0x1B) & 0xFFFFF000;   // IA32_APIC_BASE
    lapic = vmm_map_mmio(base, 4096);       // 暫存器是 MMIO，需要映射且關快取

    idt_set_gate(SPURIOUS_VEC, spurious);
    lapic_write(LAPIC_SVR, 0x100 | SPURIOUS_VEC);
    lapic_write(LAPIC_LVT0, LVT_EXTINT);    // bit 16（遮蔽）= 0
    lapic_write(LAPIC_LVT1, LVT_NMI);
}
