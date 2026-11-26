#include "mm.h"
#include "string.h"
#include "panic.h"

// 最簡單能用的 heap：一條依位址排序的區塊串列，first-fit。
//
//  HEAP_BASE
//  ┌────────┬─────────────┬────────┬──────┬────────┬───────────
//  │ header │ 使用中資料   │ header │ 空的 │ header │ …
//  └────────┴─────────────┴────────┴──────┴────────┴───────────
//
// 不夠用時，從 PMM 多拿幾頁映射到尾端，把 heap 長大。

#define MAGIC 0x4D59444F53484550ULL   // "MYDOSHEP"，用來抓 kfree 傳錯指標

struct block {
    uint64_t magic;
    size_t   size;          // 資料區大小（不含 header）
    int      free;
    struct block *next;
};
#define HDR sizeof(struct block)      // 32 byte，剛好保持 16 對齊

static struct block *head;
static uint64_t heap_end = HEAP_BASE;   // 目前映射到哪裡

static struct block *grow(size_t need) {
    size_t bytes = ALIGN_UP(need + HDR, 16 * PAGE_SIZE);   // 一次至少長 64 KB
    if (heap_end + bytes > HEAP_BASE + HEAP_MAX) return 0;
    uint64_t start = heap_end;
    for (uint64_t va = start; va < start + bytes; va += PAGE_SIZE) {
        uint64_t p = pmm_alloc();
        if (!p || vmm_map(va, p, PTE_WRITE)) return 0;
        heap_end += PAGE_SIZE;
    }
    // 接在最後一塊後面；最後一塊是空的就直接合併
    struct block *last = head;
    while (last && last->next) last = last->next;
    if (last && last->free && (uint64_t)last + HDR + last->size == start) {
        last->size += bytes;
        return last;
    }
    struct block *b = (struct block *)start;
    b->magic = MAGIC; b->size = bytes - HDR; b->free = 1; b->next = 0;
    if (last) last->next = b; else head = b;
    return b;
}

void heap_init(void) {
    if (!grow(PAGE_SIZE)) panic("HEAP: init failed");
}

void *kmalloc(size_t size) {
    if (!size) return 0;
    size = ALIGN_UP(size, 16);
    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct block *b = head; b; b = b->next) {
            if (!b->free || b->size < size) continue;
            // 剩下的空間夠放另一個區塊，就切開
            if (b->size >= size + HDR + 16) {
                struct block *rest = (struct block *)((uint8_t *)b + HDR + size);
                rest->magic = MAGIC;
                rest->size = b->size - size - HDR;
                rest->free = 1;
                rest->next = b->next;
                b->next = rest;
                b->size = size;
            }
            b->free = 0;
            return (uint8_t *)b + HDR;
        }
        if (!grow(size)) return 0;    // 找不到就長大，再找一次
    }
    return 0;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    struct block *b = (struct block *)((uint8_t *)p - HDR);
    if (b->magic != MAGIC || b->free) panic("HEAP: bad kfree(%p)", p);
    b->free = 1;
    // 和後面相鄰的空塊合併；再從頭掃一次，讓前面的空塊也吃掉它
    for (struct block *c = head; c; c = c->next) {
        while (c->free && c->next && c->next->free &&
               (uint8_t *)c + HDR + c->size == (uint8_t *)c->next) {
            c->size += HDR + c->next->size;
            c->next = c->next->next;
        }
    }
}

void heap_stats(size_t *used, size_t *freeb, size_t *mapped) {
    *used = *freeb = 0;
    for (struct block *b = head; b; b = b->next) {
        if (b->free) *freeb += b->size; else *used += b->size;
    }
    *mapped = heap_end - HEAP_BASE;
}
