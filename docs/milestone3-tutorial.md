# MYDOS 教學：里程碑 3 — 中斷、鍵盤與命令列

> **MYDOS 1.0 的更新**
>
> - 這份教學是用 Limine v9 寫的。換成 Limine v11（base revision 6）後鍵盤會沒反應，原因是 Limine v11 把 Local APIC 的 LINT0 遮蔽了，修法在[里程碑 4](milestone4-tutorial.md) 最後一節（`apic.c`），`keyboard_init` 也補上了完整的 8042 初始化。
> - 內建的 `CRASH` 改名為 `PANIC`，`CRASH` 這個名字讓給了里程碑 7 的 `CRASH.EXE`。
> - `keyboard_getc()` 改成回傳 `int`，才能表示方向鍵等特殊鍵（里程碑 8）。
> - 下面「整合到你的專案」一節是給舊專案逐步合併用的；直接使用 1.0 的 repo 就不需要。

## 目標與成果

里程碑 3 讓 MYDOS 第一次能「聽」使用者說話：按鍵會觸發中斷，核心把它轉成字元，交給一個 DOS 風格的命令列。完成後版本號是 v0.3。

完成時你會看到：

- 開機訊息逐項打勾：GDT、IDT、PIC、鍵盤
- `C:\>` 後面可以打字，Backspace、Shift、Caps Lock、Ctrl+C 都能用
- 指令（不分大小寫）：`HELP`、`VER`、`CLS`、`ECHO`、`CRASH`、`REBOOT`
- 打錯指令會回 `Bad command or file name`
- `CRASH` 會出現藍底白字的 `*** MYDOS STOP ***`，列出例外名稱和暫存器

里程碑 3 分成五步，3a（自己的 GDT）上次已完成，這份教學涵蓋 3b–3e。

## 整合到你的專案

### 新增檔案

把下面 12 個檔案放進 `src/`。Makefile 用 `$(wildcard src/*.c)`，不用改規則就會自動編到。

| 檔案 | 職責 | 步驟 |
| --- | --- | --- |
| `panic.c` / `.h` | 藍色當機畫面、`struct int_frame` | 3b |
| `idt.c` / `.h` | 256 項中斷表、32 個 CPU 例外處理 | 3b |
| `pic.c` / `.h` | 8259 PIC 重新對應、遮蔽、EOI | 3c |
| `irq.c` / `.h` | IRQ 0–15 的入口與分派表 | 3c |
| `keyboard.c` / `.h` | PS/2 scancode 轉字元、環狀緩衝區 | 3d |
| `shell.c` / `.h` | 讀一行、切指令、指令表 | 3e |

### 要改的舊檔（4 處）

**1. `term.h` / `term.c`：加 `term_clear()`**（如果還沒有）。`CLS` 和當機畫面都要用：

```c
// term.h
void term_clear(void);

// term.c：只清文字區，狀態列不動
void term_clear(void) {
    fill_rect(0, top_px, fb_w, fb_h - top_px, bg);
    cx = cy = 0;
}
```

變數名稱以你 `term.c` 裡的為準（`fb_h` 可能叫別的名字）。

**2. `kprintf.h`：公開 `kvprintf`。** 里程碑 2 的 `kprintf` 把工作交給 `kvprintf`，現在 `panic()` 也要用它：

```c
#include <stdarg.h>
void kvprintf(const char *fmt, va_list ap);
```

如果它在 `kprintf.c` 裡是 `static`，記得拿掉 `static`。

**3. `Makefile`：CFLAGS 加 `-mgeneral-regs-only`。** 意思是「整個核心只用一般暫存器」，保證中斷處理不會碰到浮點／SSE 暫存器（CPU 進中斷時不會幫你存那些）。不加在目前的旗標下也編得過，但加上比較保險。順便確認 `run` 有 `-no-reboot`。

**4. `main.c`：換掉 `kmain` 後半段。** 拿掉里程碑 2 的 60 行捲動測試，改成：

```c
#include "idt.h"
#include "irq.h"
#include "keyboard.h"
#include "shell.h"

    // …term_init、term_status("MYDOS v0.3")、term_set_color 之後：
    gdt_init();
    kprintf("[ ok ] GDT loaded\n");
    idt_init();
    kprintf("[ ok ] IDT loaded, CPU exceptions handled\n");
    irq_init();
    kprintf("[ ok ] PIC remapped to vectors 32-47\n");
    keyboard_init();
    irq_enable();                       // 從這一刻起才會收到中斷
    kprintf("[ ok ] PS/2 keyboard on IRQ1\n\n");

    shell_run();                        // 永遠不會回來
```

**順序很重要**：IDT 要在開中斷之前載好，PIC 要在開中斷之前重新對應。顛倒的話，第一個中斷就會跳到不存在的處理函式，CPU 直接重開機。

壓縮檔裡的 `reference/main.c` 是我測試用的完整版，可以對照。

## 3b：IDT 與例外處理

### 中斷是什麼

CPU 執行到一半，有兩種事會讓它「暫停手上的事去處理別的」：

| 種類 | 來源 | 例子 | 向量號碼 |
| --- | --- | --- | --- |
| 例外 | CPU 自己 | 除以零、讀不存在的記憶體 | 0–31（固定） |
| 硬體中斷 | 外部裝置經過 PIC | 按鍵、計時器 | 我們設成 32–47 |

CPU 怎麼知道該跳去哪？查 **IDT（中斷描述表）**：一張 256 格的表，第 N 格寫著「向量 N 的處理函式在哪裡」。`lidt` 指令告訴 CPU 這張表的位址。

在 3b 之前沒有 IDT，任何例外都會變成 Triple Fault，QEMU 直接重開機，你完全不知道發生什麼事——3a 那個 `const` GDT 的 bug 就是這樣。現在它會變成一個看得懂的當機畫面。

### 一格 IDT 長什麼樣

```c
struct __attribute__((packed)) idt_entry {
    uint16_t offset_lo;    // 處理函式位址 bit 0–15
    uint16_t selector;     // 0x08：跳過去時 CS 要用哪個段（3a 的 kernel code）
    uint8_t  ist;          // 0：沿用目前的堆疊
    uint8_t  type_attr;    // 0x8E
    uint16_t offset_mid;   // bit 16–31
    uint32_t offset_hi;    // bit 32–63
    uint32_t zero;
};
```

位址被拆成三段是歷史包袱：16 位元時代只有 `offset_lo`，32 位元加了 `mid`，64 位元再加 `hi`。

`0x8E` 拆開看：`1`（存在）`00`（ring 0）`0` `1110`（64 位元中斷閘）。「中斷閘」的意思是進入時 CPU 自動關中斷，處理到一半不會被另一個中斷插隊。

**IDT 陣列也不能加 `const`**，理由跟 3a 的 GDT 一樣：放進唯讀區雖然 `lidt` 不會寫它，但養成習慣比較安全，而且以後執行中可能要改表。

### 不寫組語的中斷處理函式

一般函式結束用 `ret`，中斷處理必須用 `iretq`，而且要自己保存所有暫存器。傳統做法是寫一個 `.S` 組語檔。GCC 提供另一條路：

```c
__attribute__((interrupt))
static void exc0(struct int_frame *f) { … }
```

加了 `interrupt` 屬性，GCC 會自動：保存用到的暫存器、結尾改用 `iretq`、把 CPU 推進堆疊的資料用 `f` 指標交給你。

`struct int_frame` 就是 CPU 進中斷時自動推的五個值：

```c
struct int_frame { uint64_t rip, cs, rflags, rsp, ss; };
```

`rip` 最有用：它是**出事那一行指令的位址**，用 `addr2line` 能直接查到原始碼：

```bash
addr2line -f -e build/kernel.elf 0xffffffff800028cc
# cmd_crash
# src/shell.c:56
```

### 有錯誤碼和沒錯誤碼

向量 8、10–14、17、21、29、30 這幾個例外，CPU 會多推一個錯誤碼，處理函式就得多收一個參數，否則堆疊會錯位一格、讀到的 RIP 全是錯的。所以用兩個巨集產生 32 個函式：

```c
#define EXC(n)     … exc##n(struct int_frame *f)                    { panic_exception(n, 0, 0, f); }
#define EXC_ERR(n) … exc##n(struct int_frame *f, unsigned long err) { panic_exception(n, err, 1, f); }

EXC(0) EXC(1) … EXC_ERR(8) … EXC_ERR(14) …
```

`##` 是前置處理器的「黏貼」：`exc##n` 在 `n = 14` 時變成 `exc14`。每個向量要有自己的函式，是因為 CPU 不會告訴你「這是幾號」——只能從「跳進哪個函式」來分辨。

### 當機畫面

`panic_exception` 先 `cli` 關中斷，換成藍底白字清畫面，印出例外名稱和暫存器，最後 `cli; hlt` 永久停住。Page Fault（14）額外印 **CR2**，CPU 會把「想存取但失敗的位址」放在這個暫存器。

另外還有一個一般用途的 `panic("…%d…", x)`，以後任何「不可能發生」的情況都可以呼叫它。

## 3c：PIC 重新對應

### 為什麼要搬

鍵盤、計時器這些裝置不直接接 CPU，而是接到兩顆串在一起的 **8259 PIC**：主 PIC 管 IRQ 0–7，從 PIC 管 8–15，從 PIC 接在主 PIC 的 IRQ2 上。

問題是 PIC 開機預設把 IRQ 0–7 送到向量 8–15，**剛好和 CPU 例外重疊**。鍵盤是 IRQ1，會變成向量 9；計時器 IRQ0 會變成向量 8——Double Fault。所以第一件事就是把它們搬到 32–47。

### 初始化口令

PIC 要用固定順序的四個「初始化命令字」（ICW）設定：

| 步驟 | 寫到 | 主 PIC | 從 PIC | 意思 |
| --- | --- | --- | --- | --- |
| ICW1 | 命令 port | `0x11` | `0x11` | 開始初始化，之後會有 ICW4 |
| ICW2 | 資料 port | `32` | `40` | 向量從哪裡開始 |
| ICW3 | 資料 port | `0x04` | `0x02` | 主：從 PIC 在 IRQ2（bit 2）；從：我的編號是 2 |
| ICW4 | 資料 port | `0x01` | `0x01` | 8086 模式 |

每次寫完呼叫 `io_wait()`（寫一個沒人用的 port `0x80`），給老晶片一點反應時間。

最後把兩顆都寫 `0xFF`，**遮蔽全部 IRQ**，誰需要再用 `pic_unmask()` 打開。目前只開 IRQ1，計時器還關著，不然每秒會冒出 18 次沒人處理的中斷。

### EOI：處理完要回報

PIC 送出一個中斷後，會等 CPU 回報「處理完了」（End Of Interrupt）才送下一個同等級的中斷。忘了 EOI 的症狀很典型：**鍵盤只有第一個按鍵有反應**。從 PIC 來的中斷要兩顆都通知。

`irq.c` 把這件事集中處理：16 個入口函式都呼叫 `irq_dispatch(n)`，它呼叫驅動程式註冊的函式，然後送 EOI。驅動程式只要：

```c
irq_install(1, keyboard_irq);   // 註冊 + 打開 IRQ1
```

**假中斷**：IRQ7 和 IRQ15 偶爾會因為雜訊冒出來，但其實沒有裝置要求。`pic_is_spurious()` 讀 PIC 的 ISR 暫存器確認，假的就不送 EOI（從 PIC 的假中斷只通知主 PIC）。

## 3d：PS/2 鍵盤

### Scancode

每按一個鍵，鍵盤控制器在 port `0x60` 放一個 **scancode**，然後觸發 IRQ1。scancode 是「哪個實體按鍵」的編號，不是 ASCII：

| 動作 | scancode |
| --- | --- |
| 按下 A | `0x1E` |
| 放開 A | `0x9E`（= `0x1E \| 0x80`） |
| 按下左 Shift | `0x2A` |
| 按下右 Ctrl | `0xE0` 接著 `0x1D` |

規則：**bit 7 是 1 就是放開**；`0xE0` 是延伸鍵前綴，表示下一個位元組要另外解讀。

轉換用兩張 58 格的表 `keymap[]` 和 `keymap_shift[]`，索引就是 scancode。另外記住四個狀態：`shift`、`ctrl`、`caps`、`extended`。

```c
uint8_t sc = inb(0x60);          // 一定要讀，否則控制器不會送下一個
if (sc == 0xE0) { extended = 1; return; }
int released = sc & 0x80;
uint8_t key  = sc & 0x7F;
```

小技巧：

- **Caps Lock 只影響字母**，所以用 `c ^= 0x20` 翻轉大小寫（ASCII 大小寫剛好差 bit 5）。Shift + Caps Lock 打出小寫，跟真的鍵盤一樣。
- **Ctrl + 字母** 轉成控制碼 1–26：Ctrl+C 是 3，以後 Ctrl+L 是 12。

### 環狀緩衝區

中斷處理要越快越好，不能在裡面等使用者打完一整行。所以中斷只把字元丟進一個 128 格的環狀緩衝區，`keyboard_getc()` 再慢慢拿：

```text
          tail            head
           ▼               ▼
 [ . . . . h e l l o . . . . ]
           └─ 還沒被讀的 ─┘
```

`head` 是下一個寫入位置，`tail` 是下一個讀取位置，`head == tail` 就是空的。兩個都 `% BUF_SIZE` 繞回開頭。滿了就丟掉新字，不覆蓋舊字。變數加 `volatile`，因為它們會在中斷裡被改，編譯器不能假設值沒變。

### 等待時讓 CPU 休息

```c
char keyboard_getc(void) {
    for (;;) {
        irq_disable();
        if (head != tail) { …取一個字…; irq_enable(); return c; }
        __asm__ volatile ("sti; hlt");
    }
}
```

`hlt` 讓 CPU 睡到下一個中斷，QEMU 的 CPU 使用率會從 100% 掉到接近 0。

這裡有一個經典的競爭條件：如果寫成「開中斷 → 檢查緩衝區 → hlt」，按鍵剛好發生在檢查之後、hlt 之前，CPU 就會睡著並錯過這個字，直到下一次按鍵才醒。解法利用了 x86 的一個特性：**`sti` 要等下一條指令執行完才生效**，所以 `sti; hlt` 之間不可能插進中斷——中斷一定在 hlt 開始睡之後才進來，把它叫醒。

## 3e：命令列

`shell_run()` 是一個無限迴圈：印 `C:\>`、讀一行、執行。

**讀一行**：一般字元存進陣列並回顯；Backspace 在行首時不動作，否則 `len--` 並送 `\b`（里程碑 2 的 `term_putc` 會退一格並畫空白蓋掉）；Ctrl+C 印 `^C` 放棄這一行；Enter 結束。

**切指令**：跳過前導空白，第一個空白換成 `\0`，前半是指令名，後半是參數：

```text
"  echo  hi there"  →  name = "echo"   args = "hi there"
```

**指令表**：新增指令只要在陣列加一行，不用改判斷邏輯：

```c
static const struct command commands[] = {
    { "HELP", "List commands",      cmd_help },
    { "VER",  "Show MYDOS version", cmd_ver  },
    …
};
```

比對用自己寫的 `streq_nocase()`，所以 `ver`、`VER`、`Ver` 都可以，跟 DOS 一樣。

**兩個測試指令**：

- `CRASH`：故意除以零，觸發例外 0，驗證 3b 的當機畫面。注意被除數和除數**都要是 `volatile`**——GCC 很聰明，`1 / x` 會被改寫成「x 是 1 就回 1，否則回 0」的比較運算，根本不會執行除法指令，畫面上只會印出 `0`。
- `REBOOT`：對鍵盤控制器（port `0x64`）送 `0xFE`。這是 PC/AT 留下來的怪招：鍵盤控制器有一條線接到 CPU 的 reset 腳。

## 建置與測試

```bash
cd ~/mydos
make clean && make run
```

| 測試 | 預期 | 驗證的是 |
| --- | --- | --- |
| 開機 | 四行 `[ ok ]`，最後 `C:\>` | 初始化順序 |
| 打 `help` | 列出六個指令 | 鍵盤、回顯、指令表 |
| 打 `VeR` | `MYDOS Version 0.3` | 不分大小寫 |
| 打 `dirx` 再按一次 Backspace、Enter | `Bad command or file name`，畫面上是 `dir` | `\b` 處理 |
| `echo` + Shift 打 `Hello!` | 原樣印出 | Shift 表 |
| 開 Caps Lock 打 `abc` | `ABC` | Caps Lock |
| 打一半按 Ctrl+C | `^C` 換行，新提示 | 控制碼 |
| `cls` | 清空，狀態列還在 | `term_clear` |
| `crash` | 藍色畫面，`CPU exception 0: Divide Error` | IDT、例外處理 |
| 當機後亂按 | 沒反應 | `cli` |

**常見問題：**

| 症狀 | 原因 | 解法 |
| --- | --- | --- |
| 一開中斷就重開機 | IDT 沒載好，或 PIC 沒重新對應 | 檢查 `kmain` 順序；確認 `-no-reboot` |
| 一按鍵就出現 `Double Fault` | PIC 還在預設的向量 8–15 | 確認 `irq_init()` 在 `irq_enable()` 之前 |
| 鍵盤只有第一個鍵有反應 | 沒送 EOI 或沒讀 port `0x60` | 檢查 `irq_dispatch` 和 `keyboard_irq` |
| 按鍵沒任何反應 | IRQ1 沒解除遮蔽，或忘了 `sti` | `irq_install(1, …)`、`irq_enable()` |
| 字母全部錯位 | scancode 表少一格或多一格 | 對照表：`0x10` 是 `q`、`0x1E` 是 `a`、`0x2C` 是 `z` |
| 每個字出現兩次 | 放開也被當成按下 | 檢查 `released` 判斷 |
| 當機畫面的 RIP 很奇怪 | 有錯誤碼的例外用了 `EXC` 而不是 `EXC_ERR` | 對照向量 8、10–14、17、21、29、30 |
| `undefined reference to term_clear` / `kvprintf` | 舊檔還沒加 | 見「要改的舊檔」 |
| `crash` 只印出 `0` | 編譯器把除法最佳化掉了 | 被除數也要 `volatile` |

確認無誤後 commit：

```bash
git add -A && git commit -m "Milestone 3: IDT, PIC, keyboard, shell (v0.3)"
git push
```

## 練習題與下一步

由簡單到難：

- [ ] 加 `COLOR` 指令：`COLOR 1F` 換成藍底白字（DOS 的配色編號）
- [ ] 讓 `HELP` 改用 `%-8s`：先替 `kprintf` 加上 `-` 左對齊旗標
- [ ] 在 `CRASH` 旁邊加 `PF` 指令，讀位址 `0x1234` 觸發 Page Fault，確認 CR2 顯示正確
- [ ] 支援方向鍵：`0xE0 0x48` 是上鍵，做出「上一個指令」的歷史紀錄
- [ ] 進階：打開 IRQ0（計時器，約 18.2 Hz），在狀態列右邊顯示開機秒數

**下一步：里程碑 4 記憶體管理**——讀 Limine 給的記憶體地圖，做實體頁框配置器，接著是分頁和 `kmalloc`。有了它，命令列才能存歷史紀錄、以後才能載入程式。
