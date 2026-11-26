# MYDOS 教學：里程碑 4 — 記憶體管理

## 目標與成果

到里程碑 3 為止，MYDOS 只用了兩種記憶體：編譯時就決定好的全域變數，和 Limine 給的堆疊。想要「需要多少就拿多少」——讀一個檔案、存指令歷史、載入程式——就得自己管記憶體。完成後版本號是 v0.4。

完成時你會看到：

- 開機訊息多一行 `[ ok ] Memory: 254 MB free`
- 新指令 `MEM`，列出實體記憶體和核心 heap 的使用量
- **在 Limine v11 下鍵盤恢復正常**（里程碑 3 留下的問題，見最後一節）

這個里程碑分三層，由下往上：

| 層 | 檔案 | 管什麼 | 單位 |
| --- | --- | --- | --- |
| 實體頁框配置器（PMM） | `pmm.c` | 哪些實體 RAM 還沒人用 | 一頁 4 KB |
| 分頁（VMM） | `vmm.c` | 虛擬位址對應到哪一頁實體記憶體 | 一頁 4 KB |
| 核心 heap | `heap.c` | `kmalloc(37)` 這種任意大小 | byte |

另外 `string.c` 收納了原本散在 `main.c` 的 `memcpy` 等函式，再加上 `strcmp`、`strcpy` 這些之後一直會用到的工具。

## 先搞懂三種位址

| 名稱 | 例子 | 說明 |
| --- | --- | --- |
| 實體位址 | `0x0000000001234000` | RAM 晶片上真正的位置 |
| HHDM 位址 | `0xFFFF800001234000` | 實體位址 + 固定位移，Limine 幫我們映射好的 |
| 一般虛擬位址 | `0xFFFFC00000000000` | 我們自己決定要映射到哪一頁 |

CPU 開了分頁之後，**程式裡寫的每個位址都是虛擬位址**，要經過頁表翻譯才變成實體位址。所以拿到一個實體頁之後，核心不能直接寫 `*(uint64_t *)0x1234000`，要先轉成虛擬位址。

Limine 幫了大忙：它把**整個實體記憶體**平移映射到 `0xFFFF800000000000` 開始的地方，叫 HHDM（Higher Half Direct Map）。所以：

```c
static inline void *phys_to_virt(uint64_t phys) { return (void *)(phys + hhdm_offset); }
```

`hhdm_offset` 不是固定值，要用 `limine_hhdm_request` 問 Limine。

MYDOS 的虛擬位址配置圖寫在 `mm.h` 開頭：

```text
0x0000000000400000  使用者程式（里程碑 7）
0x00007FFFFFFF0000  使用者堆疊頂端（里程碑 7）
0xFFFF800000000000  HHDM：整個實體記憶體
0xFFFFC00000000000  核心 heap
0xFFFFD00000000000  MMIO（裝置暫存器）
0xFFFFFFFF80000000  核心程式本身
```

## PMM：用點陣圖記錄哪些頁還空著

### 記憶體地圖

RAM 不是一整塊都能用：有的被 BIOS 佔了，有的是 ACPI 表，有的是 framebuffer。Limine 會給一份**記憶體地圖**（`limine_memmap_request`），每一項是「起點、長度、類型」。只有 `LIMINE_MEMMAP_USABLE` 能拿來用。

### 點陣圖

每一頁用 1 bit 記錄：1 = 使用中，0 = 空著。管 4 GB 只要 1M 頁 ÷ 8 = 128 KB。

`pmm_init` 做四件事：

1. 找出最高的可用位址，決定點陣圖要多大
2. 在地圖裡找一塊夠大的可用區，**把點陣圖本身放在那裡**
3. 先把整張點陣圖填 1（全部使用中），再把可用區清成 0
4. 把點陣圖自己佔的頁、和實體位址 0 標回使用中

第 3 步的順序很重要：**預設不可用、確定可用才開放**。反過來寫的話，地圖沒列到的洞（例如 640 KB–1 MB 的老 BIOS 區）就會被當成可用配出去。

實體位址 0 被保留，是因為 `pmm_alloc()` 用 0 代表「沒記憶體了」。

### 配置

```c
uint64_t pmm_alloc(void) {
    for (uint64_t n = 0; n < max_pages; n++) {
        uint64_t i = (next_hint + n) % max_pages;
        if (!test_bit(i)) { set_bit(i); used_pages++; next_hint = i + 1; return i * PAGE_SIZE; }
    }
    return 0;
}
```

`next_hint` 記住上次找到的位置，下次從那裡往後找，不用每次從頭掃。`pmm_free` 會檢查「這頁真的是使用中嗎」，重複釋放會直接 panic——這種 bug 越早炸越好找。

## VMM：自己動手改頁表

### 四層頁表

x86-64 把 48 位元的虛擬位址切成五段：

```text
 47    39 38    30 29    21 20    12 11       0
[ PML4  ][ PDPT  ][  PD   ][  PT   ][ 頁內位移 ]
   9 bit    9 bit    9 bit    9 bit    12 bit
```

每一層都是 512 格 × 8 byte = 剛好一頁。CPU 從 CR3 暫存器找到 PML4，用第一段當索引找到 PDPT，一路走到 PT，最後一格就是實體頁的位址。每格的低 12 bit 是旗標：

| bit | 名稱 | 意思 |
| --- | --- | --- |
| 0 | Present | 這格有效 |
| 1 | Write | 可寫 |
| 2 | User | ring 3 可以存取（里程碑 7 會用到） |
| 4 | Cache Disable | MMIO 一定要設 |
| 7 | Huge | 這格直接指向 2 MB / 1 GB 的大頁，不再往下 |

### 沿用 Limine 的頁表

我們不重建頁表，直接讀 CR3 拿到 Limine 建好的那一份，在上面**加**自己的映射。核心、HHDM、framebuffer 都已經在裡面了。

```c
static uint64_t *next_level(uint64_t *table, int idx, int create, uint64_t flags) {
    uint64_t e = table[idx];
    if (e & PTE_PRESENT) return phys_to_virt(e & PTE_ADDR);
    if (!create) return 0;
    uint64_t page = pmm_alloc_zero();          // 新的一層表要清成 0
    table[idx] = page | PTE_PRESENT | PTE_WRITE;
    return phys_to_virt(page);
}
```

`vmm_map(va, pa, flags)` 走三次 `next_level` 拿到最底層那一格，填進實體位址和旗標，最後執行 `invlpg`。

**`invlpg` 不能忘。** CPU 會把翻譯結果快取在 TLB 裡。改了頁表不通知它，CPU 可能繼續用舊的翻譯，症狀是「明明映射了卻 Page Fault」或「寫到別的地方去」，而且時有時無，非常難查。

**Huge page 的坑：** Limine 映射 HHDM 時用了 2 MB 或 1 GB 的大頁。如果想在那個範圍裡映射 4 KB 的小頁，就得先把大頁拆開。MYDOS 選擇不碰那些範圍（heap 和 MMIO 放在獨立的 PML4 格子），遇到了就 panic 提醒。

## kmalloc：在虛擬位址上切小塊

heap 放在 `0xFFFFC00000000000`。一開始先映射 64 KB，不夠再長。

### 資料結構

每塊記憶體前面放一個 32 byte 的標頭，串成一條依位址排序的串列：

```text
┌────────┬─────────────┬────────┬──────┬────────┬───────────
│ header │ 使用中資料   │ header │ 空的 │ header │ …
└────────┴─────────────┴────────┴──────┴────────┴───────────
```

```c
struct block {
    uint64_t magic;      // "MYDOSHEP"，抓 kfree 傳錯指標
    size_t   size;       // 資料區大小
    int      free;
    struct block *next;
};
```

標頭剛好 32 byte，所以資料區永遠 16 byte 對齊。

### kmalloc：first-fit + 切割

從頭找第一塊「空的而且夠大」的。如果剩下的空間還放得下另一個標頭加 16 byte，就切成兩塊，後半塊維持空的。整條找完都沒有，就呼叫 `grow()` 從 PMM 多拿幾頁、映射到 heap 尾端，再找一次。

### kfree：合併

標成空的之後，掃一遍串列，把**相鄰而且都空著**的區塊併成一塊。不合併的話，heap 會被切成一堆小碎片，明明總共有很多空間卻配不出大塊——這叫**外部碎片化**。

`magic` 欄位是除錯神器：`kfree` 一個不是 `kmalloc` 給的指標，或是 free 兩次，都會被抓到並 panic，而不是默默搞壞 heap、幾分鐘後在完全不相干的地方當機。

### 驗證

我用一段壓力測試跑過：50 輪、每輪 200 次隨機大小的配置與交錯釋放，再配一塊 8 MB。全部釋放後 `heap_stats` 顯示 used = 0，而且 free 剛好等於 mapped 減一個標頭——代表所有碎片都合併回一整塊了。

## 修好 Limine v11 的鍵盤

### 症狀

里程碑 3 的程式在 Limine v9 下打字正常，換成 v11（base revision 6）後完全沒反應。用 QEMU monitor 檢查：

```text
(qemu) info pic
pic0: irr=03 imr=fd ...       ← IRQ1 有進來（irr bit 1）、也沒被遮蔽（imr bit 1 = 0）
(qemu) info lapic
LVT0  0x00018700  masked  ExtINT   ← 但 LAPIC 的 LINT0 被遮蔽了
```

### 原因

現代 CPU 每顆核心都有一個 **Local APIC**。老式 8259 PIC 的輸出接在 LAPIC 的 **LINT0** 腳上，再由 LAPIC 交給 CPU。Limine v11 開機時把 LINT0 遮蔽了，所以 PIC 收到按鍵、CPU 卻永遠等不到中斷。

### 解法：virtual wire 模式

`apic.c` 把 LINT0 設成 **ExtINT**，意思是「PIC 送來什麼就原封不動轉給 CPU」：

```c
uint64_t base = rdmsr(0x1B) & 0xFFFFF000;   // 從 MSR 讀出 LAPIC 的實體位址
lapic = vmm_map_mmio(base, 4096);           // ← 這就是為什麼要先有 VMM
lapic_write(LAPIC_SVR,  0x100 | 0xFF);      // 啟用 APIC，假中斷用向量 255
lapic_write(LAPIC_LVT0, 7 << 8);            // LINT0 = ExtINT，不遮蔽
lapic_write(LAPIC_LVT1, 4 << 8);            // LINT1 = NMI
```

LAPIC 的暫存器是 **MMIO**：看起來像記憶體，讀寫其實是在跟硬體講話。所以映射時要加 `PTE_NOCACHE`，否則 CPU 可能把寫入暫存在快取裡，硬體根本沒收到。HHDM 只映射 RAM，不包含這種裝置位址，所以要自己用 VMM 映射——這是里程碑 4 先做記憶體、再修鍵盤的原因。

順便也把 8042 鍵盤控制器的初始化補完整（`keyboard_init`）：關鍵盤埠、清緩衝區、讀設定位元組、打開 IRQ1 和 scancode 轉譯、再開回鍵盤埠。原則是**不要相信開機程式留下的硬體狀態**。

## 測試

```bash
make clean && make run
```

| 測試 | 預期 |
| --- | --- |
| 開機 | `[ ok ] Memory: 254 MB free`（QEMU `-m 256M`） |
| 打字 | 正常（Limine v11） |
| `MEM` | Physical 約 266,000,000 bytes，Used 不到 100 KB；Kernel heap 65,536 mapped |

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| `VMM: address is inside a huge page` | 想映射的位址落在 Limine 的大頁範圍，換一個位址區 |
| 映射後寫入沒效果或寫錯地方 | 漏了 `invlpg` |
| 開機就 `PMM: no room for the page bitmap` | QEMU 記憶體給太少，`-m` 至少 64M |
| `HEAP: bad kfree` | free 了兩次，或傳了不是 kmalloc 給的指標 |
| 鍵盤在 v11 沒反應 | `apic_init()` 沒呼叫，或在 `vmm_init()` 之前呼叫 |

## 練習題

- [ ] 寫 `krealloc(p, new_size)`：原地夠大就直接回傳，不夠就配新的、複製、釋放舊的
- [ ] 讓 `MEM` 多顯示 heap 有幾個區塊、最大的空塊多大（觀察碎片化）
- [ ] 進階：在 `vmm_map` 遇到 huge page 時，把它拆成 512 個小頁再繼續
- [ ] 進階：把 `pmm_alloc` 改成一次掃 64 bit（`uint64_t` 全是 1 就跳過），比較速度
