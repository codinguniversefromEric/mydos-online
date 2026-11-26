#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096ULL
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((uint64_t)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))

// ---- 虛擬位址配置圖（高半部是核心，低半部留給使用者程式）----
// 0x0000000000400000  使用者程式載入位址（M7）
// 0x00007FFFFFFF0000  使用者堆疊頂端（M7）
// 0xFFFF800000000000  HHDM：整個實體記憶體的直接映射（Limine 建好的）
// 0xFFFFC00000000000  核心 heap（kmalloc）
// 0xFFFFD00000000000  MMIO 映射區（LAPIC 等）
// 0xFFFFFFFF80000000  核心程式本身
#define HEAP_BASE  0xFFFFC00000000000ULL
#define HEAP_MAX   (256ULL * 1024 * 1024)
#define MMIO_BASE  0xFFFFD00000000000ULL

// HHDM：實體位址 + 位移 = 核心可以直接用的虛擬位址
extern uint64_t hhdm_offset;
static inline void *phys_to_virt(uint64_t phys) { return (void *)(phys + hhdm_offset); }

// ---- 實體頁框配置器（pmm.c）----
void     pmm_init(struct limine_memmap_response *map);
uint64_t pmm_alloc(void);           // 回傳一頁的實體位址；用完回傳 0
uint64_t pmm_alloc_zero(void);      // 同上，並清成 0
void     pmm_free(uint64_t phys);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);

// ---- 分頁（vmm.c）----
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_NOCACHE  (1ULL << 4)    // MMIO 一定要關快取
#define PTE_HUGE     (1ULL << 7)
#define PTE_ADDR     0x000FFFFFFFFFF000ULL

void     vmm_init(void);
int      vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);  // 0 = 成功
uint64_t vmm_unmap(uint64_t virt);                                // 回傳原本的實體位址
uint64_t vmm_translate(uint64_t virt);                            // 查不到回傳 0
void    *vmm_map_mmio(uint64_t phys, size_t size);                // 映射裝置暫存器

// ---- 核心 heap（heap.c）----
void   heap_init(void);
void  *kmalloc(size_t size);
void  *kzalloc(size_t size);
void   kfree(void *p);
void   heap_stats(size_t *used, size_t *free, size_t *mapped);
