# MYDOS 教學：里程碑 2 — 文字終端、捲動與 kprintf

> **MYDOS 1.0 的更新**
>
> - 60 行的捲動測試已經從 `kmain` 拿掉，開機畫面改成逐項打勾的 `[ ok ]` 訊息。
> - `kprintf` 在[里程碑 5](milestone5-tutorial.md) 重構過：格式化邏輯只寫一次，輸出目的地可以抽換，多了 `ksnprintf`（印到字串）和 `%-8s` 左對齊。
> - `term.c` 後來陸續加了 `term_clear`、`term_goto`、狀態列右邊的時鐘；[網頁版](web-tutorial.md)還讓它同時送出 ANSI 跳脫碼。
> - `main.c` 裡的 `memcpy` 等函式搬到了 `string.c`。

## 目標與成果

里程碑 2 讓 MYDOS 從「在固定座標畫字」進步成「像真的終端機一樣輸出文字」：自動換行、畫面滿了往上捲、用 `kprintf` 格式化輸出。完成後版本號是 v0.2。

完成時你會看到：

- 頂端藍綠色狀態列 `MYDOS v0.2`，捲動時不會被蓋掉
- 下方 60 行測試輸出一路捲到 `line 59`
- 一行格式測試：`[  -42] [-0042] [ok] [Z] [%] [-9223372036854775808]`
- 最後停在 `C:\>` 加底線游標
- WSL 終端機（COM1）同步印出一模一樣的文字

前置條件：里程碑 1 已完成，也就是 Limine 開機、framebuffer、8x8 字型、COM1 都能動。

## 專案結構

原則是一個檔案只做一件事。`main.c` 只負責開機檢查和呼叫別人，畫字、捲動、格式化都交給專門的模組。

| 檔案 | 職責 | 誰會用它 |
| --- | --- | --- |
| `src/main.c` | Limine 請求、`mem*` 函式、`kmain` 進入點 | Limine |
| `src/kprintf.c` / `.h` | 格式化字串，把字元逐一交給終端 | `main.c`，以後所有模組 |
| `src/term.c` / `.h` | 游標、換行、捲動、狀態列、畫字 | `kprintf.c` |
| `src/serial.c` / `.h` | COM1 初始化與送字元 | `term.c`、`main.c` |
| `src/io.h` | `outb` / `inb` 兩個 inline asm | `serial.c`，以後的鍵盤、PIC |
| `src/font8x8_basic.h` | 128 個字元的 8x8 點陣 | 只有 `term.c` |

呼叫方向是單向往下：

```text
kmain ──▶ kprintf ──▶ term_putc ──┬──▶ framebuffer（畫面）
                                  └──▶ serial_putc ──▶ COM1（WSL 終端機）
```

下層不知道上層存在，所以以後換掉其中一層（例如改用更大的字型）不會牽動別人。

兩個設計決定值得記住：

1. **為什麼把序列埠拆出來**：里程碑 1 的 `serial_putc` 是 `static`，只有 `main.c` 看得到。`term.c` 也需要它，所以搬到 `serial.c`，並在 `serial.h` 公開宣告。
2. **為什麼字型只能被一個 `.c` include**：`font8x8_basic.h` 裡寫的是 `char font8x8_basic[128][8] = {…};`，這是**定義**，不只是宣告。兩個 `.c` 都 include 它，連結器會看到兩份同名陣列，報 `multiple definition` 錯誤。所以只讓 `term.c` include，`main.c` 改成完全不碰字型。

## Framebuffer 基礎

螢幕就是一塊記憶體：寫一個 32 位元數字進去，對應的像素就變色。Limine 把這塊記憶體的位址和尺寸交給你，放在 `struct limine_framebuffer` 裡。

| 欄位 | 意思 |
| --- | --- |
| `address` | 第一個像素的位址 |
| `width` / `height` | 可見像素數 |
| `pitch` | **一列佔幾個 byte** |
| `bpp` | 每像素幾個 bit（我們要求 32） |

32 bpp 時一個像素是 `0x00RRGGBB`，例如 `0x008080` 是藍綠色、`0xC0C0C0` 是 DOS 灰。

座標 (x, y) 的像素位址：

```text
addr = address + y × pitch + x × 4
```

**pitch 是最常見的坑。** 硬體為了對齊，每一列結尾可能多留幾個 byte，所以 pitch 可能大於 `width × 4`。在 QEMU 裡兩者剛好相等，程式寫錯也看不出來；換到真機或別的解析度，畫面就會斜掉。規則很簡單：**換列一律乘 pitch，列內才乘 4。**

`term.c` 把這條規則包成一個小函式，之後所有畫圖都經過它：

```c
static inline uint32_t *row_ptr(uint64_t y) {
    return (uint32_t *)(fb_base + y * fb_pitch);   // fb_base 是 uint8_t*，以 byte 為單位前進
}
// 用法：row_ptr(y)[x] = color;
```

`main.c` 也加了一道檢查：`fb->bpp != 32` 就從序列埠報錯並停機，避免畫出一堆雜訊卻不知道原因。

## 畫一個字

每個字元是 8 個 byte，一個 byte 是一列，一個 bit 是一個像素。`font8x8_basic['A']` 長這樣（`#` 是 1）：

```text
byte 0x0C   ..##....
byte 0x1E   .####...
byte 0x33   ##..##..
byte 0x33   ##..##..
byte 0x3F   ######..
byte 0x33   ##..##..
byte 0x33   ##..##..
byte 0x00   ........
```

注意 `0x0C` 是二進位 `00001100`，畫出來卻是 `..##....`。這是因為這套字型規定**最低位元（bit 0）是最左邊的像素**，所以檢查第 `col` 個像素要用 `bits & (1 << col)`。如果換一套「bit 7 在左」的字型，畫出來會左右顛倒，那時要改成 `1 << (7 - col)`。

原本 8x8 在高解析度下太小，所以每個點放大成 `SCALE × SCALE`（2×2）的小方塊，一個字變成 16×16：

```c
for (int row = 0; row < 8; row++) {
    uint8_t bits = (uint8_t)font8x8_basic[c][row];   // 字型是 char，先轉成無號
    for (int col = 0; col < 8; col++) {
        uint32_t color = (bits & (1 << col)) ? f : b;
        for (int sy = 0; sy < SCALE; sy++) {
            uint32_t *p = row_ptr(py + row * SCALE + sy) + px + col * SCALE;
            for (int sx = 0; sx < SCALE; sx++)
                p[sx] = color;
        }
    }
}
```

和里程碑 1 有兩個差別：

- **背景也要畫。** 里程碑 1 只畫亮的點，因為背景本來就是黑的。終端機會在同一格反覆寫字（例如 Backspace 後再打字），只畫前景會跟舊字疊在一起。
- **先轉成 `uint8_t`。** 字型陣列的型別是 `char`，在 x86 上是有號的。`0x80` 以上的值會變成負數，位移運算就可能出錯。

## 終端狀態與 term_putc

終端機的核心是一個游標 `(cx, cy)`，單位是「格」而不是像素。每收到一個字元，就決定要畫字還是移動游標。

**狀態：**

| 變數 | 意思 |
| --- | --- |
| `cols`, `rows` | 文字區有幾欄幾列 |
| `cx`, `cy` | 游標所在的格子 |
| `top_px` | 文字區從第幾個像素列開始（狀態列在它上面） |
| `fg`, `bg` | 目前的前景、背景色 |

格子換算成像素：`x = MARGIN + cx × 16`、`y = top_px + cy × 16`。

**每個字元怎麼處理：**

| 字元 | 動作 |
| --- | --- |
| `\n` | 換行：`cx = 0`，`cy + 1`；超過最後一列就捲動 |
| `\r` | 回到行首：`cx = 0` |
| `\b` | 退一格，並在那格畫空白蓋掉舊字 |
| `\t` | 跳到下一個 8 的倍數：`cx = (cx + 8) & ~7` |
| 其他 | 在 `(cx, cy)` 畫字，`cx + 1`；到行尾自動換行 |

`(cx + 8) & ~7` 這招值得記：`~7` 是二進位結尾三個 0，`& ~7` 會把數字往下對齊到 8 的倍數。先加 8 再對齊，就是「下一個」8 的倍數。

**狀態列保留區。** `term_status()` 在頂端畫藍綠色橫條，然後把 `top_px` 設在它下面，並重新計算 `rows`。之後所有畫字、捲動、清除都只碰 `top_px` 以下，狀態列自然不會被蓋掉。

**游標。** 每次 `term_putc` 開頭先用背景色把舊游標擦掉，結尾在新位置畫一條 3 像素高的底線。

**同步到序列埠。** `term_putc` 一開始就把字元送給 `serial_putc`。序列終端機要 `\r\n` 才會回到行首，所以遇到 `\n` 先補送一個 `\r`。

## 捲動

捲動就是把文字區整塊往上搬一列字（16 個像素列），再把空出來的最後一列塗成背景色。不需要記住每格是什麼字，因為 framebuffer 本身就是記錄。

```c
static void scroll(void) {
    uint8_t *dst = fb_base + top_px * fb_pitch;          // 文字區第 0 列
    uint8_t *src = dst + CELL_H * fb_pitch;              // 文字區第 1 列
    uint64_t total = (uint64_t)(rows - 1) * CELL_H * fb_pitch;

    uint64_t n8 = total / 8;                             // 一次搬 8 byte
    uint64_t *d8 = (uint64_t *)dst, *s8 = (uint64_t *)src;
    for (uint64_t i = 0; i < n8; i++)
        d8[i] = s8[i];
    for (uint64_t i = n8 * 8; i < total; i++)            // 剩下不足 8 byte 的補完
        dst[i] = src[i];

    fill_rect(0, cell_y(rows - 1), fb_w, CELL_H, bg);    // 清最後一列
}
```

三個重點：

1. **全部用 pitch 算。** 搬的是整列記憶體，包含每列結尾的對齊空間，所以位移和長度都用 `pitch`，不用 `width × 4`。
2. **從前往後搬是安全的。** 來源在目的地後面（位址較大），往前搬不會覆蓋還沒讀的資料。反方向（往下捲）就得從尾巴搬，這正是 `memmove` 要分兩種方向的原因。
3. **一次搬 8 byte。** 比一次 1 byte 快約 8 倍。編譯旗標有 `-mno-sse`，不能用 SSE 一次搬 16 byte，`uint64_t` 是目前最寬的選擇。

**效能提醒：** 在真機上，從顯示記憶體「讀」比寫慢很多，捲動會明顯卡頓。常見解法是在一般記憶體保留一份影子緩衝區，捲動時讀影子、寫螢幕。QEMU 裡感覺不到，所以先記在待辦。

## kprintf

`kprintf` 是核心版的 `printf`：逐字掃格式字串，一般字元直接交給 `term_putc`，遇到 `%` 就從參數裡取一個值轉成文字。

**可變參數。** `<stdarg.h>` 是編譯器內建的 header，不屬於 C 標準函式庫，所以 `-ffreestanding` 下照樣能用：

```c
void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);     // 從 fmt 之後開始取參數
    kvprintf(fmt, ap);     // 真正的工作在這裡，以後 panic() 之類也能重用
    va_end(ap);
}
```

**一個 `%` 後面依序解析四段：**

1. 旗標：`0` 表示用 0 補齊，否則用空白
2. 寬度：一串數字，例如 `%08x` 的 `8`
3. 長度：`l` 或 `ll`，表示參數是 64 位元
4. 轉換字元：`c s d i u x X p %`

**長度修飾為什麼重要。** `va_arg` 必須拿對型別。傳進來的是 `int`（4 byte）卻用 `long long` 取，會讀到隔壁的垃圾。所以 `%d` 用 `va_arg(ap, int)`、`%lld` 用 `va_arg(ap, long long)`，印 64 位元值一定要寫 `ll`。

**數字轉字串**用「反覆除以底數」，餘數就是最低位，所以存進 buffer 時順序是反的，印出時倒過來：

```c
do {
    buf[len++] = digits[u % base];   // 例：255, base 16 → 'f', 'f'
    u /= base;
} while (u);                         // 用 do-while，0 才會印出 "0"
while (len) term_putc(buf[--len]);
```

**負數和補零的順序：** `%05d` 印 -42 要得到 `-0042`，負號在 0 前面；`%5d` 要得到 `  -42`，空白在負號前面。所以負號的位置要看 `pad` 是 `'0'` 還是 `' '`。

**最小的負數。** int64 最小值是 -9223372036854775808，而最大值只有 9223372036854775807，直接 `-v` 會溢位。做法是先轉成無號再取負：`(uint64_t)0 - (uint64_t)v`，無號運算的溢位是有定義的，結果剛好正確。測試行最後一格就是在驗證這件事。

**`%p`** 固定印 `0x` 加 16 位十六進位，因為 64 位元位址剛好 16 位，對齊後比較好讀。

## Makefile

舊的 Makefile 只寫了 `build/main.o` 一條規則，新加的 `.c` 不會被編譯，連結時就會出現 `undefined reference to term_init`。新版改成自動抓 `src/` 底下所有 `.c`：

```makefile
SRCS := $(wildcard src/*.c)                       # src/main.c src/term.c ...
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))     # build/main.o build/term.o ...
DEPS := $(OBJS:.o=.d)                             # build/main.d build/term.d ...

build/%.o: src/%.c | limine-protocol
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

-include $(DEPS)
```

| 寫法 | 作用 |
| --- | --- |
| `$(wildcard src/*.c)` | 列出所有 `.c`，以後加 `keyboard.c` 不用改 Makefile |
| `build/%.o: src/%.c` | 樣式規則：任何 `build/X.o` 都從 `src/X.c` 編出來 |
| `$<` / `$@` | 第一個相依檔（`.c`）/ 目標（`.o`） |
| `-MMD` | 編譯時順便輸出 `.d` 檔，記錄這個 `.c` include 了哪些 `.h` |
| `-MP` | 替每個 `.h` 加空規則，刪掉 header 時不會卡住 |
| `-include $(DEPS)` | 讀入 `.d`；前面的 `-` 代表第一次還沒有 `.d` 也不報錯 |
| `\| limine-protocol` | 只需要它存在，它更新不會觸發重編 |

效果：改了 `term.h`，`main.c`、`kprintf.c`、`term.c` 會自動重編，`serial.c` 不受影響。

**縮排一定要用 Tab。** Makefile 的指令行必須以 Tab 開頭，貼上時被換成空白會出現 `missing separator` 錯誤。VS Code 右下角可以確認目前用的是 Tab 還是 Spaces。

## 建置、測試與除錯

```bash
cd ~/mydos
make clean && make run
```

`kmain` 裡的測試會刻意印 60 行，超過一個畫面，用來驗證捲動。對照下面這張表檢查：

| 檢查點 | 預期 | 驗證的是 |
| --- | --- | --- |
| 狀態列 | 藍綠底白字 `MYDOS v0.2`，一直在最上面 | 保留區、捲動範圍 |
| 最後一行數字 | `line 59`，前面的行被捲上去 | 捲動 |
| `hex=` 欄 | `0003b000`，固定 8 位 | `%08x` |
| `ptr=` 欄 | `0xffff8000…`，固定 16 位 | `%p` |
| 格式測試行 | `[  -42] [-0042] [ok] [Z] [%] [-9223372036854775808]` | 補空白、補零、最小負數 |
| 結尾 | `C:\>` 加底線游標 | 游標 |
| WSL 終端機 | 同樣內容，沒有多餘空行 | COM1 同步 |

`ptr=` 開頭是 `0xffff8000`，因為堆疊位在 Limine 的高位直接映射區（HHDM），這是正常的。

**常見問題：**

| 症狀 | 原因 | 解法 |
| --- | --- | --- |
| `undefined reference to term_init` | Makefile 沒編到新檔案 | 換成新版 Makefile |
| `multiple definition of font8x8_basic` | 兩個 `.c` 都 include 了字型 | 只留 `term.c` include |
| `missing separator` | Makefile 指令行用了空白 | 改成 Tab |
| 字左右顛倒 | 字型是 bit 7 在左 | 改用 `1 << (7 - col)` |
| 畫面斜掉、字錯位 | 換列用了 `width × 4` | 一律用 `pitch` |
| 捲動後最下面有殘影 | 沒清最後一列 | 檢查 `scroll()` 結尾的 `fill_rect` |
| WSL 視窗每行之間多一空行 | `\r\n` 補了兩次 | 拿掉其中一邊的 `\r` |
| QEMU 一開就重開機 | CPU 例外，目前還沒有 IDT 能顯示原因 | 先看 COM1 最後一行印到哪；里程碑 3 會補例外處理 |

確認無誤後 commit：

```bash
git add -A && git commit -m "Milestone 2: terminal, scrolling, kprintf"
```

## 練習題與下一步

動手改一改，最能確認自己真的懂了。由簡單到難：

- [ ] 把 `SCALE` 改成 1 和 3，看 `cols`、`rows` 怎麼跟著變
- [ ] 用 `kprintf` 印出 `cols` 和 `rows`（提示：要在 `term.h` 加一個查詢函式）
- [ ] 加上 `%b`，以二進位印數字，例如 `%08b` 印 5 得到 `00000101`
- [ ] 加上 `%-5d` 左對齊（數字在左，空白補在右邊）
- [ ] 寫 `term_set_cursor(col, row)`，在狀態列右邊印時鐘位置的佔位文字
- [ ] 進階：加一份影子緩衝區，讓 `scroll()` 只讀一般記憶體、不讀 framebuffer

**下一步：里程碑 3 鍵盤輸入**，做完就能在 `C:\>` 後面真的打字：

1. GDT 和 IDT：設定段描述表和中斷表，順便加 CPU 例外處理，當機時畫面會顯示原因
2. PIC 重新對應：把 IRQ 0–15 移到向量 32–47，開啟 IRQ1（鍵盤）
3. PS/2 鍵盤驅動：scancode 轉 ASCII，處理 Shift 和 Backspace
4. 最簡單的命令列：讀一行、按 Enter 回顯，之後加 `VER`、`CLS`、`HELP`

里程碑 2 寫好的 `kprintf` 和 `\b` 處理，到時候都會直接用上。
