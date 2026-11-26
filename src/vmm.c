#include "mm.h"
#include "panic.h"

// x86-64 四層頁表：一個虛擬位址被切成 4 個 9-bit 索引 + 12-bit 頁內位移
//
//  63      48 47    39 38    30 29    21 20    12 11       0
//  [ 符號延伸 ][ PML4 ][ PDPT ][  PD  ][  PT  ][ 頁內位移 ]
//
// 每一層都是 512 格 × 8 byte = 剛好一頁。

static uint64_t *pml4;   // 頂層表（HHDM 位址）

static inline uint64_t read_cr3(void) { uint64_t v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void invlpg(uint64_t va) { __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory"); }

void vmm_init(void) {
    // 沿用 Limine 建好的頁表：它已經映射好核心、HHDM、framebuffer。
    // 我們只在上面「加」自己的映射。
    pml4 = phys_to_virt(read_cr3() & PTE_ADDR);
}

// 走到下一層；不存在且 create=1 時就配一頁新表
static uint64_t *next_level(uint64_t *table, int idx, int create, uint64_t flags) {
    uint64_t e = table[idx];
    if (e & PTE_PRESENT) {
        if (e & PTE_HUGE) panic("VMM: address is inside a huge page");
        // 使用者頁面要求每一層都有 USER bit，否則 ring 3 存取會 Page Fault
        if ((flags & PTE_USER) && !(e & PTE_USER)) table[idx] = e | PTE_USER;
        return phys_to_virt(e & PTE_ADDR);
    }
    if (!create) return 0;
    uint64_t page = pmm_alloc_zero();
    if (!page) return 0;
    table[idx] = page | PTE_PRESENT | PTE_WRITE | (flags & PTE_USER);
    return phys_to_virt(page);
}

static uint64_t *get_pte(uint64_t va, int create, uint64_t flags) {
    uint64_t *t = pml4;
    t = next_level(t, (va >> 39) & 511, create, flags); if (!t) return 0;
    t = next_level(t, (va >> 30) & 511, create, flags); if (!t) return 0;
    t = next_level(t, (va >> 21) & 511, create, flags); if (!t) return 0;
    return &t[(va >> 12) & 511];
}

int vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pte = get_pte(virt, 1, flags);
    if (!pte) return -1;
    *pte = (phys & PTE_ADDR) | flags | PTE_PRESENT;
    invlpg(virt);           // 讓 CPU 忘掉這個位址舊的快取（TLB）
    return 0;
}

uint64_t vmm_unmap(uint64_t virt) {
    uint64_t *pte = get_pte(virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    uint64_t phys = *pte & PTE_ADDR;
    *pte = 0;
    invlpg(virt);
    return phys;
}

uint64_t vmm_translate(uint64_t virt) {
    uint64_t *pte = get_pte(virt, 0, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    return (*pte & PTE_ADDR) | (virt & (PAGE_SIZE - 1));
}

void *vmm_map_mmio(uint64_t phys, size_t size) {
    static uint64_t next = MMIO_BASE;
    uint64_t start = ALIGN_DOWN(phys, PAGE_SIZE);
    uint64_t end = ALIGN_UP(phys + size, PAGE_SIZE);
    uint64_t va = next;
    for (uint64_t p = start; p < end; p += PAGE_SIZE, next += PAGE_SIZE)
        if (vmm_map(next, p, PTE_WRITE | PTE_NOCACHE)) panic("VMM: MMIO map failed");
    return (void *)(va + (phys - start));
}
