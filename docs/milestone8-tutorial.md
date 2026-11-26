# MYDOS 教學：里程碑 8 — 批次檔、AUTOEXEC.BAT 與指令歷史（v1.0）

## 目標與成果

最後一個里程碑補上讓 DOS「好用」的三樣東西：把指令存成檔案自動執行的**批次檔**、開機自動執行的 **AUTOEXEC.BAT**，以及用方向鍵叫回之前打過的指令。完成後就是 **MYDOS 1.0**。

完成時你會看到：

- 開機後 AUTOEXEC.BAT 印出歡迎訊息，取代原本的 `Type HELP…`
- `DEMO` 或 `DEMO Ada` 執行 `C:\BIN\DEMO.BAT`，示範所有批次檔功能
- 按 ↑ ↓ 瀏覽最近 16 個指令，按 F3 叫回上一個（DOS 的老習慣）
- 新指令：`REM` `PAUSE` `GOTO` `IF` `CALL`，`ECHO` 多了 `ON` / `OFF`

## 批次檔

### 原理

批次檔就是一個文字檔，每一行是一個指令。`batch_run()` 把整個檔案讀進來，一行一行交給 `shell_execute()`——跟你在鍵盤上打的完全一樣。所以**每個內建指令、每支程式，在批次檔裡自動都能用**。

```text
C:\>type \bin\demo.bat
@ECHO OFF
REM DEMO.BAT - a quick tour of batch file features
ECHO Hello %1! This is %0.
IF "%1"=="" ECHO (Tip: try DEMO yourname)
IF EXIST \README.TXT ECHO README.TXT is on drive C.
IF NOT EXIST \NOPE.TXT ECHO NOPE.TXT does not exist, as expected.
ECHO Running HELLO.EXE from a batch file:
HELLO from batch
IF ERRORLEVEL 1 GOTO failed
ECHO HELLO exited with code 0.
GOTO end
:failed
ECHO Something went wrong.
:end
ECHO Done.
```

找程式的 `find_program()` 在里程碑 7 就會依序試 `.EXE` 和 `.BAT`，所以打 `DEMO` 就會找到 `\BIN\DEMO.BAT`。

### 每一行的處理順序

```text
1. 去掉 \r（DOS 文字檔的換行是 \r\n）
2. 空行、:標籤 → 跳過
3. 開頭 @ → 這一行不顯示
4. 把 %0–%9 換成參數，%% 換成 %
5. ECHO 開著而且沒有 @ → 先印出 C:\>指令
6. shell_execute(指令)
7. 剛剛執行的是 GOTO → 跳到標籤那一行
```

### 功能一覽

| 寫法 | 作用 |
| --- | --- |
| `@指令` | 這一行不顯示 |
| `ECHO OFF` / `ECHO ON` | 關掉／打開「執行前顯示指令」 |
| `ECHO.` | 印一個空行 |
| `REM 文字` | 註解 |
| `PAUSE` | `Press any key to continue . . .` |
| `%0` `%1` … `%9` | 批次檔名和參數 |
| `:標籤` + `GOTO 標籤` | 跳轉（不分大小寫） |
| `IF [NOT] EXIST 檔案 指令` | 檔案存在才執行 |
| `IF [NOT] ERRORLEVEL n 指令` | 上一支程式的結束碼 ≥ n 才執行 |
| `IF [NOT] 字串1==字串2 指令` | 字串相等才執行 |
| `CALL 別的.BAT` | 執行別的批次檔 |

### GOTO 怎麼跳

`GOTO` 是一個普通的內建指令，但它不能自己移動「目前讀到哪一行」——那是 `batch_run` 迴圈的區域變數。所以 `GOTO` 只把標籤存進 `goto_label`，`batch_run` 執行完每一行都檢查一次，有值就從檔案開頭找 `:標籤`，把讀取位置移過去。

### IF 是遞迴

`IF EXIST X ECHO hi` 的做法是：判斷條件，成立的話把剩下的字串 `ECHO hi` **再丟給 `shell_execute`**。所以 IF 後面可以接任何東西，包括另一個 IF 或 GOTO。

`IF "%1"==""` 是 DOS 的經典技巧：沒給參數時 `%1` 換成空字串，變成 `""==""`。加引號是為了避免空字串讓整行少一個字、語法壞掉。

### ERRORLEVEL

程式 `main` 的回傳值（或 `exit(n)`）就是結束碼，命令列存在 `shell_errorlevel`。被強制結束時是負數（當掉 -1、Ctrl+C -2）。`IF ERRORLEVEL 1` 的意思是「≥ 1」，這是 DOS 的規則，所以要從大的往小的判斷。

### 巢狀與中止

批次檔可以呼叫批次檔，最多 4 層（`MAX_DEPTH`），超過就拒絕，避免無限遞迴吃光記憶體。和原版 DOS 有一點不同：DOS 直接打另一個批次檔的名字時**不會回來**，要用 `CALL` 才會回來；MYDOS 一律會回來，`CALL` 保留下來只是為了相容。

執行到一半按 Ctrl+C，每一行開始前會檢查，問 `Terminate batch job (Y/N)?`，跟 DOS 一樣。

## AUTOEXEC.BAT

`shell_run()` 一開始檢查 `\AUTOEXEC.BAT`，有的話就執行：

```c
if (!fs_stat("\\AUTOEXEC.BAT", &info)) batch_run("\\AUTOEXEC.BAT", "");
else kprintf("Type HELP for a list of commands.\n\n");
```

內容在專案的 `disk/AUTOEXEC.BAT`，用 `\r\n` 換行，跟真的 DOS 文字檔一樣。想改開機訊息，直接改這個檔再 `make`，或在 MYDOS 裡用 `COPY CON \AUTOEXEC.BAT` 重寫。

## 指令歷史

### 方向鍵

方向鍵是**延伸鍵**：鍵盤先送 `0xE0`，再送一般的 scancode。里程碑 3 只認右 Ctrl，現在多認四個：

| scancode | 鍵 | 送進緩衝區的值 |
| --- | --- | --- |
| `E0 48` | ↑ | `KEY_UP`（0x80） |
| `E0 50` | ↓ | `KEY_DOWN`（0x81） |
| `E0 4B` | ← | `KEY_LEFT`（0x82） |
| `E0 4D` | → | `KEY_RIGHT`（0x83） |
| `3D` | F3 | `KEY_F3`（0x84） |

特殊鍵用 0x80 以上的值表示，因為 ASCII 只到 0x7F，不會撞到。這也是 `keyboard_getc()` 從回傳 `char` 改成 `int` 的原因：`char` 在 x86 上是有號的，0x80 會變成 -128。

### 環狀陣列

```c
#define HISTORY 16
static char history[HISTORY][LINE_MAX + 1];
static int  hist_count;      // 總共存過幾筆，可能超過 16
```

第 n 筆存在 `history[n % 16]`，超過 16 筆時最舊的自動被蓋掉。`browse` 記錄目前瀏覽到第幾筆，按 ↑ 減一、↓ 加一，等於 `hist_count` 時代表「新的一行」（空白）。

叫回一筆時，`replace_line()` 先送一串 `\b` 把目前這行擦掉，再印出歷史內容。空行和跟上一筆一樣的指令不存，跟大部分 shell 一樣。

## 從乾淨的狀態測試

1.0 的 Makefile 會在第一次建置時自動下載 Limine 和 limine-protocol，所以可以模擬「別人第一次拿到這個專案」：

```bash
git clone 你的repo mydos-test && cd mydos-test
make            # 自動下載 Limine、編核心、編程式、做 ISO 和 disk.img
make run
```

我用這個方式從頭建置，零警告，接著跑完整的流程：

| 測試 | 預期 |
| --- | --- |
| 開機 | 九行 `[ ok ]`，AUTOEXEC.BAT 的歡迎訊息 |
| `DEMO` | 提示「try DEMO yourname」，HELLO 執行，最後 `Done.` |
| `DEMO Ada` | `Hello Ada! This is DEMO.BAT.`，沒有提示那行 |
| `ECHO` | `ECHO is on` |
| 打幾個指令後按 ↑ ↑ Enter | 執行倒數第二個指令 |
| F3 | 叫回上一個指令 |
| 自己寫一個有 PAUSE 的批次檔 | 停住等按鍵 |
| 批次檔執行中按 Ctrl+C | `Terminate batch job (Y/N)?` |
| 全部做完關掉 QEMU，`fsck.fat -n disk.img` | 沒有錯誤 |

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| 批次檔每行最後一個字怪怪的 | 沒去掉 `\r` |
| `Label not found` | 標籤打錯，或 `:標籤` 後面多了空白以外的字 |
| `IF "%1"==""` 說 Syntax error | `==` 兩邊不能有空白 |
| ↑ 印出奇怪字元 | 延伸鍵沒處理，`0x48` 被當成 `8` |
| 開機沒執行 AUTOEXEC.BAT | 檔案不在根目錄，或檔名不是 8.3 大寫 |

## MYDOS 1.0 全貌

```text
                  ┌───────────────────────────────────────┐
  ring 3          │  HELLO  GUESS  WC  UPPER  LIFE  CRASH │  user/programs + user/lib
                  └──────────────────┬────────────────────┘
                                     │ int 0x80
  ─────────────────────────────────────────────────────────────────────
                  ┌──────────────────┴────────────────────┐
  命令列          │ shell.c  cmd_fs.c  batch.c             │
                  ├────────────────────────────────────────┤
  服務            │ process.c syscall.c  fat.c  kprintf.c  │
                  ├────────────────────────────────────────┤
  記憶體          │ pmm.c  vmm.c  heap.c                   │
                  ├────────────────────────────────────────┤
  中斷            │ gdt.c  idt.c  panic.c  irq.c  pic.c    │
                  │ apic.c                                 │
                  ├────────────────────────────────────────┤
  驅動            │ term.c  serial.c  keyboard.c  timer.c  │
  ring 0          │ rtc.c  ata.c                           │
                  └────────────────────────────────────────┘
                              Limine → kmain (main.c)
```

| 里程碑 | 版本 | 內容 |
| --- | --- | --- |
| 1 | v0.1 | Limine 開機、framebuffer、字型、COM1 |
| 2 | v0.2 | 終端機、捲動、kprintf |
| 3 | v0.3 | GDT、IDT、例外、PIC、鍵盤、命令列 |
| 4 | v0.4 | 實體記憶體、分頁、kmalloc、LAPIC |
| 5 | v0.5 | PIT 計時器、RTC、狀態列時鐘 |
| 6 | v0.6 | ATA 硬碟、FAT16 讀寫、檔案指令 |
| 7 | v0.7 | ring 3、系統呼叫、ELF 載入、範例程式 |
| 8 | v1.0 | 批次檔、AUTOEXEC.BAT、指令歷史 |

## 接下來可以往哪裡走

MYDOS 1.0 是一個完整的單工 DOS。往後的方向，大致依難度排列：

**讓它更像 DOS**
- [ ] `SET` 和環境變數，`PATH` 可以設定
- [ ] `MORE`、`FIND`、`SORT` 和管線 `|`、重新導向 `>`
- [ ] 全螢幕文字編輯器 `EDIT.EXE`

**讓它在真機上跑**
- [ ] 用影子緩衝區加速捲動（真機讀 framebuffer 很慢）
- [ ] 支援 AHCI（現代電腦的 SATA 介面，ATA PIO 在很多新主機板上已經不存在）
- [ ] USB 鍵盤（很多筆電沒有 PS/2 模擬）

**你一開始想要的「AI 時代」方向**——MYDOS 已經有了基礎：程式跑在 ring 3、每個動作都要經過系統呼叫，核心可以檢查、記錄、拒絕。
- [ ] 系統呼叫記錄：每支程式做了什麼都寫進 `\LOG\`，人和 AI 都能讀
- [ ] 權限清單：程式宣告它需要什麼（只讀某個資料夾、不能刪檔），核心強制執行
- [ ] 可還原的變更：`fs_write_file` 覆蓋前先把舊版本存起來，加一個 `UNDO` 指令
- [ ] 可讀的系統狀態：`SYSINFO` 輸出結構化的 JSON，讓 AI 直接理解系統現況

**作業系統的下一個台階**
- [ ] 多工：計時器中斷時切換到另一支程式（需要每支程式一份頁表和一組暫存器）
- [ ] 繁體中文：換成 16×16 的點陣字型和 Big5 或 UTF-8 支援
