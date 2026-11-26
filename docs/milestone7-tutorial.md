# MYDOS 教學：里程碑 7 — 執行程式（使用者模式）

## 目標與成果

到這裡為止，MYDOS 能做的事都是寫死在核心裡的指令。這個里程碑讓它能**從磁碟載入並執行程式**，而且程式跑在受保護的 **ring 3**：程式當掉、亂寫記憶體、想碰硬體，都只會結束那支程式，系統繼續運作。完成後版本號是 v0.7。

這是 MYDOS 跟原版 DOS 最大的不同。DOS 的程式擁有整台電腦，一支程式寫錯就整台當機；MYDOS 從設計上就假設「程式可能有 bug、甚至可能是 AI 寫的」，所以核心必須保護自己。

完成時你會看到：

- `C:\BIN` 裡有六支程式：`HELLO` `GUESS` `WC` `UPPER` `LIFE` `CRASH`
- 打名字就能執行，不用打 `.EXE`，也不用在 `\BIN` 裡（像 DOS 的 PATH）
- `CRASH` 示範四種壞事，全部被擋下來：

```text
C:\>crash
Writing to address 0 (null pointer)...
Page Fault in program at 0x0000000000400139 (exception 14, address 0x0000000000000000)
Program terminated.
C:\>
```

| 檔案 | 職責 |
| --- | --- |
| `gdt.c` | 加上 ring 3 的段描述子和 TSS |
| `process.c` | 進入／離開 ring 3、ELF 載入、使用者記憶體 |
| `syscall.c`、`syscall_nums.h` | `int 0x80` 系統呼叫 |
| `user/lib/` | 程式用的迷你 C 函式庫 |
| `user/programs/` | 六支範例程式 |

## Ring 0 和 Ring 3

x86 有四個權限等級，實際只用兩個：

| | Ring 0（核心） | Ring 3（使用者程式） |
| --- | --- | --- |
| 可以存取的記憶體 | 全部 | 只有頁表標了 User bit 的頁 |
| `in` / `out`（碰硬體） | 可以 | #GP 例外 |
| `cli` / `hlt` / `lidt` / 改 CR3 | 可以 | #GP 例外 |

CPU 怎麼知道現在是哪個 ring？看 **CS 暫存器的最低 2 bit**。所以 GDT 要多兩個 DPL=3 的段：

```c
0x00CFF2000000FFFF,   // 0x18 user data（F2：DPL 3、資料、可寫）
0x00AFFA000000FFFF,   // 0x20 user code（FA：DPL 3、程式碼、64 位元）
```

選擇子要加上 RPL 3：程式碼段 `0x20 | 3 = 0x23`、資料段 `0x18 | 3 = 0x1B`。

## TSS：中斷時要換到哪個堆疊

程式在 ring 3 跑的時候，堆疊是程式自己的。這時發生中斷（按鍵、計時器、系統呼叫），CPU 跳進核心——但核心**不能用程式的堆疊**：程式可能把 rsp 設成亂七八糟的值，核心一推東西就毀了。

所以 CPU 從 ring 3 進 ring 0 時會自動換堆疊，換成哪一個寫在 **TSS**（Task State Segment）的 `rsp0` 欄位：

```c
struct tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;   // ← 進 ring 0 時用的堆疊
    uint64_t reserved1;
    uint64_t ist[7];             // 「緊急」堆疊
    …
    uint16_t iomap_base;
};
```

TSS 在 GDT 裡佔兩格（16 byte 的描述子），用 `ltr 0x28` 載入。

順便用了 `ist[0]`：**Double Fault 改用獨立的乾淨堆疊**（IDT 的 IST 欄位設 1）。Double Fault 常常是堆疊爆掉造成的，在原本的堆疊上處理只會變成 Triple Fault 重開機。

`iomap_base = sizeof(tss)` 代表「沒有 I/O 權限表」，ring 3 碰任何 port 都是 #GP。這就是 `CRASH PORT` 被擋下來的原因。

## 進入 ring 3：假裝從中斷返回

CPU 沒有「跳到 ring 3」的指令，但 `iretq`（中斷返回）會依照堆疊上的值恢復 CS——如果堆疊上寫的是 ring 3 的 CS，就回到 ring 3 了。所以我們**自己做一個假的中斷框架**：

```text
push 0x1B          SS     使用者資料段
push 使用者 rsp     RSP
push 0x202         RFLAGS 中斷開啟（IF）
push 0x23          CS     使用者程式碼段
push 進入點         RIP
iretq              → CPU「返回」到 ring 3 的進入點
```

這段在 `process.c` 的 `user_enter`，是 MYDOS 少數一定要用組語的地方（要精確控制堆疊）。進去之前把所有通用暫存器清零，避免把核心的資料漏給程式看。

### 怎麼回來？

程式結束時呼叫 `exit` 系統呼叫，這時 CPU 在核心、但在 `rsp0` 那個堆疊上，離命令列呼叫 `process_run` 的地方很遠。解法是 setjmp/longjmp 的概念：

- `user_enter` 一開始把六個「被呼叫者保存暫存器」推上堆疊，並把 rsp 存進 `saved_rsp`
- `user_leave(code)` 把 rsp 換回 `saved_rsp`、彈出那六個暫存器、`ret`

效果是：**`user_enter` 好像正常回傳了，回傳值就是結束碼**。

```c
int code = user_enter(entry, rsp, argc, argv);   // 程式跑完才「回來」
```

`saved_rsp` 不能是 `static`：組語用名字引用它，編譯器卻看不到 C 程式碼在用它，會把它最佳化掉，連結時報 `undefined reference`。

## 系統呼叫

程式不能碰硬體，那要怎麼印字、讀檔？請核心代勞。約定（跟 Linux 很像）：

```text
rax = 系統呼叫編號     rdi, rsi, rdx = 參數
int $0x80
rax = 回傳值
```

IDT 第 0x80 格的 DPL 要設成 3，ring 3 才能用 `int` 指令觸發它，否則也是 #GP。

### 入口要用組語

之前的中斷處理都用 `__attribute__((interrupt))`，但它拿不到 rax、rdi 這些暫存器的值。所以 `syscall_entry` 是一小段組語：把 15 個通用暫存器全部推上堆疊，堆疊頂端剛好排成一個 `struct regs`，把它的位址交給 C：

```c
struct regs {
    uint64_t r15, r14, …, rbx, rax;   // 順序跟 push 相反
    struct int_frame frame;            // CPU 自己推的 rip cs rflags rsp ss
};

void syscall_dispatch(struct regs *r) {
    switch (r->rax) {
    case SYS_PUTS: … break;
    …
    }
    r->rax = ret;      // 改堆疊上的值，pop 回去之後程式就看到回傳值
}
```

系統呼叫編號放在 `src/syscall_nums.h`，**核心和使用者函式庫 include 同一個檔案**，兩邊永遠不會對不上。

| 編號 | 名稱 | 用途 |
| --- | --- | --- |
| 0 | `exit` | 結束程式 |
| 1 | `puts` | 印字 |
| 2–4 | `getkey` `pollkey` `readline` | 鍵盤 |
| 5–9 | `open` `read` `write` `close` `delete` | 檔案 |
| 10–12 | `ticks` `sleep` `time` | 時間 |
| 13–16 | `cls` `color` `screen` `gotoxy` | 螢幕 |

### 永遠不要相信程式給的指標

程式傳一個指標說「請印出這裡的字串」，核心不能直接讀——那可能是核心自己的記憶體位址（偷資料），或根本沒映射（核心 Page Fault 就藍畫面了）。所以每個指標都要先檢查：

```c
int user_ptr_ok(const void *ptr, uint64_t len) {
    uint64_t a = (uint64_t)ptr;
    if (a < USER_BASE || a + len < a || a + len > USER_LIMIT) return 0;   // 範圍、溢位
    for (每一頁) if (!vmm_translate(p)) return 0;                         // 真的有映射
    return 1;
}
```

`a + len < a` 是在檢查整數溢位：程式可能傳一個很大的 `len` 讓加法繞回去，騙過範圍檢查。

### 檔案代號

`open` 把整個檔案讀進核心的緩衝區，回傳一個小整數（從 3 開始，0–2 保留給鍵盤和螢幕，沿用 Unix 習慣）。`read` 從緩衝區複製。寫入模式則是先累積在緩衝區，`close` 時才呼叫 `fs_write_file` 存檔。程式結束時核心會自動關掉所有沒關的檔案。

## 載入 ELF

程式的檔案格式是 **ELF**（Linux 用的那種），只是副檔名取名 `.EXE` 致敬 DOS。ELF 開頭是一個標頭，指向一張 **program header 表**，每一項說「檔案的第 X byte 開始、長 Y byte，要放到記憶體位址 Z」：

```c
for (每個 type == PT_LOAD 的 program header) {
    檢查 vaddr 在 0x400000 到堆疊底之間
    映射 [vaddr, vaddr + memsz) 的每一頁（User | Write）
    memcpy(vaddr, file + offset, filesz);
    memset(vaddr + filesz, 0, memsz - filesz);    // .bss：全域變數初始為 0
}
```

`memsz` 比 `filesz` 大的部分就是 `.bss`。例如 `LIFE.EXE` 有兩個 80×50 的陣列，佔記憶體但不佔檔案空間。

因為 MYDOS 和程式共用同一張頁表（核心在高半部、程式在低半部），核心可以直接 `memcpy` 到程式的位址。程式結束時，`free_user_memory()` 把記錄下來的每一頁取消映射、還給 PMM。

**參數**放在使用者堆疊頂端：先放字串，再放 `argv` 指標陣列，最後把 rsp 設成 `argv - 8`。減 8 是 x86-64 的呼叫慣例：進入函式時 `rsp + 8` 要是 16 的倍數（就像剛被 `call` 進來、推了一個返回位址）。

## 使用者函式庫

程式不能用 Linux 的 libc（那裡面的系統呼叫是 Linux 的）。`user/lib/libc.c` 是一個迷你版：

```c
static inline int64_t syscall3(int64_t n, int64_t a, int64_t b, int64_t c) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return ret;
}

void _start(int argc, char **argv) { exit(main(argc, argv)); }
```

inline asm 的約束字母：`a` = rax、`D` = rdi、`S` = rsi、`d` = rdx。`_start` 是真正的進入點，核心把 argc、argv 放在 rdi、rsi，剛好就是 C 函式的前兩個參數。

另外有 `printf`、字串函式、亂數，和一個「只往前切」的 `malloc`：從 1 MB 的靜態陣列切，`free` 什麼都不做。程式結束時核心會收回全部記憶體，對小程式來說很夠用。

`user/user.ld` 讓程式從 `0x400000` 開始；`user/Makefile` 把 `programs/hello.c` 編成 `build/HELLO.EXE`（轉大寫符合 8.3），`make` 時自動放進 `C:\BIN`。

**寫新程式**只要在 `user/programs/` 加一個 `.c`，`make` 就會出現在 `C:\BIN`。

## 程式當掉怎麼辦

### CPU 例外

`panic_exception` 一開始先看 CS：

```c
if ((f->cs & 3) == 3 && process_running())
    process_fault(vec, exc_names[vec], err, f);   // 印訊息、user_leave(-1)
bluescreen();                                      // 核心自己出事才藍畫面
```

`CRASH` 的四種情況：

| 指令 | 做了什麼 | CPU 的反應 |
| --- | --- | --- |
| `CRASH` | 寫位址 0 | Page Fault：那頁沒映射 |
| `CRASH KERNEL` | 讀 `0xFFFFFFFF80000000` | Page Fault：核心頁沒有 User bit |
| `CRASH PORT` | `outb` 到鍵盤控制器想重開機 | General Protection：ring 3 不能 I/O |
| `CRASH LOOP` | 無窮迴圈 | 不會出錯，但按 Ctrl+C 可以結束 |

### Ctrl+C

無窮迴圈不會觸發例外，要靠鍵盤。鍵盤中斷看到 Ctrl+C 而且有程式在跑，就設 `process_break = 1`。但**不能在鍵盤中斷裡直接結束程式**：那時還沒送 EOI，跳走之後 PIC 永遠等不到 EOI，鍵盤就死了。

所以在兩個安全的時機檢查：

1. `irq_dispatch` **送完 EOI 之後**，如果被打斷的是 ring 3（`f->cs & 3`），結束程式——計時器每 10 ms 一次，所以無窮迴圈最慢 10 ms 內就會被抓到
2. 系統呼叫結束、要回 ring 3 之前

程式正在 `readline` 等輸入時按 Ctrl+C，`sys_readline` 會立刻返回，讓第 2 點接手。

## 測試

```text
C:\>hello one two
Hello from user mode (ring 3)!
argc = 3
  argv[0] = "HELLO.EXE"
  argv[1] = "one"
  argv[2] = "two"
C:\>wc readme.txt
     11 lines      56 words     343 bytes  readme.txt
C:\>upper readme.txt shout.txt
343 bytes written to shout.txt
C:\>guess
C:\>life
```

| 測試 | 預期 |
| --- | --- |
| `CRASH` / `CRASH KERNEL` / `CRASH PORT` | 印出例外、`Program terminated.`，回到提示字元 |
| `CRASH LOOP` 再按 Ctrl+C | `^C`，回到提示字元 |
| `GUESS` 中途 Ctrl+C | 立刻結束 |
| `LIFE` | 綠色全螢幕動畫，按任意鍵結束、顏色還原 |
| 連續跑十次程式後 `MEM` | Used 不變（沒有記憶體洩漏） |
| `PANIC` | 核心自己的藍畫面（測試用，會停機） |

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| 一進程式就 #GP | GDT 使用者段的 DPL 不是 3，或選擇子沒加 RPL 3 |
| 程式第一次系統呼叫就 #GP | IDT 0x80 的 DPL 不是 3 |
| 程式一進去就 Page Fault，CR2 是程式位址 | 頁表中間某一層沒有 User bit（`next_level` 要補上） |
| 系統呼叫回來後 Triple Fault | TSS 的 `rsp0` 沒設，或 `ltr` 沒執行 |
| 程式結束後鍵盤沒反應 | 從中斷裡結束程式前沒送 EOI；或 `user_enter` 回來後沒 `sti` |
| `undefined reference to saved_rsp` | 變數宣告成 `static` |
| 結束程式後命令列變綠色 | 被強制結束時沒還原顏色（`run_program` 處理） |

## 練習題

- [ ] 寫 `ECHOARGS.EXE`，把參數倒過來印
- [ ] 加一個系統呼叫 `SYS_READDIR`，寫 `LS.EXE` 列出目錄
- [ ] 讓命令列記住上一支程式的結束碼，`ECHO %ERRORLEVEL%` 印出來
- [ ] 進階：寫一支 `TYPEWRIT.EXE` 文字編輯器（用 `gotoxy` 和 `getkey`，方向鍵已經有 `KEY_UP` 等代碼）
- [ ] 進階（AI 時代方向）：給每支程式一份「權限清單」，例如 `WC.EXE` 只能讀檔、不能寫檔，系統呼叫時檢查
