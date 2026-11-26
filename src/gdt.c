#include "gdt.h"

// 64 位元模式幾乎不用分段了，GDT 只剩兩個用途：
//   1. 告訴 CPU 現在是 ring 0（核心）還是 ring 3（使用者程式）
//   2. 指向 TSS
//
// 段描述子的值是固定的「魔術數字」，拆開看：
//   0x00AF9A000000FFFF：Present、ring 0、程式碼、可讀、L=1（64 位元）
//   0x00CF92000000FFFF：Present、ring 0、資料、可寫
//   0x00AFFA… / 0x00CFF2…：同上，但 DPL=3（ring 3 可以用）

// TSS（任務狀態段）：在 64 位元模式只剩一個重要工作——
// 程式在 ring 3 時發生中斷，CPU 要切換到哪一個核心堆疊（rsp0）。
struct __attribute__((packed)) tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];          // 「緊急用」堆疊，例如 Double Fault
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static struct tss tss;
static uint8_t df_stack[8192] __attribute__((aligned(16)));   // Double Fault 專用堆疊

static uint64_t gdt[7] = {
    0,
    0x00AF9A000000FFFF,   // 0x08 kernel code
    0x00CF92000000FFFF,   // 0x10 kernel data
    0x00CFF2000000FFFF,   // 0x18 user data
    0x00AFFA000000FFFF,   // 0x20 user code
    0, 0,                 // 0x28 TSS（16 byte，執行時填）
};

struct __attribute__((packed)) gdtr { uint16_t limit; uint64_t base; };

void gdt_init(void) {
    tss.iomap_base = sizeof(tss);           // 沒有 I/O 權限表：ring 3 碰任何 port 都會 #GP
    tss.ist[0] = (uint64_t)df_stack + sizeof(df_stack);

    uint64_t base = (uint64_t)&tss, limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
             (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    struct gdtr r = { sizeof(gdt) - 1, (uint64_t)gdt };
    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"                    // 遠程返回：同時換掉 CS
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1: movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n" "movw %%ax, %%es\n" "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n" "movw %%ax, %%gs\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"                       // 載入 TSS
        :: "m"(r) : "rax", "memory");
}

void tss_set_kernel_stack(uint64_t rsp0) { tss.rsp0 = rsp0; }
