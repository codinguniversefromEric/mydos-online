#include "mm.h"
#include "string.h"
#include "panic.h"

// 點陣圖：每一頁一個 bit，1 = 使用中，0 = 可用。
// 管 4 GB 記憶體只需要 1M 頁 / 8 = 128 KB 的點陣圖。
uint64_t hhdm_offset;   // Limine 告訴我們的 HHDM 位移，在 kmain 設定

static uint8_t  *bitmap;
static uint64_t  max_pages;     // 點陣圖涵蓋的頁數（到最高的可用位址為止）
static uint64_t  total_pages;   // 可用頁總數
static uint64_t  used_pages;
static uint64_t  next_hint;     // 上次找到空頁的位置，下次從這裡往後找

static inline void set_bit(uint64_t i)   { bitmap[i / 8] |=  (1 << (i % 8)); }
static inline void clear_bit(uint64_t i) { bitmap[i / 8] &= ~(1 << (i % 8)); }
static inline int  test_bit(uint64_t i)  { return bitmap[i / 8] & (1 << (i % 8)); }

void pmm_init(struct limine_memmap_response *map) {
    // 1. 找出最高的可用位址，決定點陣圖要多大
    uint64_t top = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > top)
            top = e->base + e->length;
    }
    max_pages = top / PAGE_SIZE;
    uint64_t bitmap_bytes = ALIGN_UP((max_pages + 7) / 8, PAGE_SIZE);

    // 2. 找一塊夠大的可用區放點陣圖本身
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmap_bytes) {
            bitmap = phys_to_virt(e->base);
            break;
        }
    }
    if (!bitmap) panic("PMM: no room for the page bitmap");

    // 3. 先全部標成「使用中」，再把可用區清成 0。
    //    這樣保留區、ACPI、framebuffer 永遠不會被配出去。
    memset(bitmap, 0xFF, bitmap_bytes);
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        for (uint64_t p = e->base / PAGE_SIZE; p < (e->base + e->length) / PAGE_SIZE; p++) {
            clear_bit(p);
            total_pages++;
        }
    }

    // 4. 點陣圖自己佔的頁，和實體位址 0（當成「沒有」用）要標回使用中
    uint64_t bm_phys = (uint64_t)bitmap - hhdm_offset;
    for (uint64_t p = bm_phys / PAGE_SIZE; p < (bm_phys + bitmap_bytes) / PAGE_SIZE; p++) {
        set_bit(p);
        used_pages++;
    }
    if (!test_bit(0)) { set_bit(0); used_pages++; }
}

uint64_t pmm_alloc(void) {
    for (uint64_t n = 0; n < max_pages; n++) {
        uint64_t i = (next_hint + n) % max_pages;
        if (!test_bit(i)) {
            set_bit(i);
            used_pages++;
            next_hint = i + 1;
            return i * PAGE_SIZE;
        }
    }
    return 0;   // 記憶體用完了
}

uint64_t pmm_alloc_zero(void) {
    uint64_t p = pmm_alloc();
    if (p) memset(phys_to_virt(p), 0, PAGE_SIZE);
    return p;
}

void pmm_free(uint64_t phys) {
    uint64_t i = phys / PAGE_SIZE;
    if (i >= max_pages || !test_bit(i)) panic("PMM: bad free %p", (void *)phys);
    clear_bit(i);
    used_pages--;
}

uint64_t pmm_total_pages(void) { return total_pages; }
uint64_t pmm_free_pages(void)  { return total_pages - used_pages; }
