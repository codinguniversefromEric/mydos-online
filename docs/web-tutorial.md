# MYDOS 教學：網頁版 — 在瀏覽器裡開機

## 目標與成果

讓任何人打開一個網址就能用 MYDOS，不用裝 WSL、QEMU，也不用編譯。

完成時你會有：

- `make serve-web`：在本機 http://localhost:8000 預覽
- `./tools/publish-github-pages.sh`：一個指令放上 `https://你的帳號.github.io/mydos-online/`
- 之後每次 push，GitHub Actions 自動重新建置、部署

網頁上看到的**不是假的畫面**：瀏覽器裡真的跑著一台模擬的 x86-64 PC，從 BIOS → Limine → MYDOS 核心一路開機，跟 `make run` 完全一樣的 `mydos.iso` 和 `disk.img`。

## 為什麼是 QEMU Wasm

瀏覽器裡跑 PC 模擬器最有名的是 v86，但它**只支援 32 位元**，而 MYDOS 是 64 位元長模式。[QEMU Wasm](https://github.com/ktock/qemu-wasm) 是把 QEMU 本身用 Emscripten 編成 WebAssembly，支援 x86_64，而且作者把預先編好的檔案放在 GitHub 上，我們直接拿來用（`tools/fetch-qemu-wasm.sh`，固定在某個 commit）。

代價是它很大：`qemu-system-x86_64.wasm` 約 40 MB，第一次載入要等一下，之後瀏覽器會快取。

這個預編版本**沒有圖形輸出**，只有序列埠接到網頁上的終端機（xterm.js）。好在 MYDOS 從里程碑 2 開始，每個字都會同步送到 COM1——當初為了在 WSL 終端機上看輸出而做的設計，現在剛好派上用場。

## MYDOS 這邊要改的兩件事

### 1. 序列埠也要能當鍵盤

網頁版沒有 PS/2 鍵盤，按鍵經由序列埠傳進來。COM1 收到資料時，線路狀態暫存器（port `0x3FD`）的 bit 0 會亮：

```c
int serial_getc(void) {
    if (!(inb(COM1 + 5) & 0x01)) return -1;   // 沒有資料
    return inb(COM1);
}
```

計時器每 10 ms 呼叫一次 `keyboard_serial_poll()`，把收到的字元塞進跟 PS/2 共用的鍵盤緩衝區。所以命令列、程式、批次檔完全不知道字是從哪裡來的。

但終端機送來的是**文字**，不是 scancode，有幾個要翻譯：

| 終端機送來 | 意思 | 翻成 |
| --- | --- | --- |
| `\r` | Enter | `\n` |
| `0x7F` | Backspace | `\b` |
| `ESC [ A` / `B` / `C` / `D` | 方向鍵 | `KEY_UP` … |
| `ESC O R` | F3 | `KEY_F3` |
| 單獨一個 `ESC` | Esc 鍵 | `27` |

方向鍵是三個字元的序列，用一個小狀態機處理：收到 `ESC` 進狀態 1、收到 `[` 進狀態 2、再收到字母就輸出對應的鍵。麻煩的是**單獨按 Esc** 也是送 `ESC`，無法立刻分辨。解法是等一下：`ESC` 之後 30 ms 都沒有下一個字，才當成 Esc 鍵。

### 2. 畫面動作要變成 ANSI 跳脫碼

清畫面、移動游標、換顏色這些動作原本只改 framebuffer，序列埠那頭看不到。網頁版只看得到序列埠，所以要同時送 ANSI 跳脫碼：

| 動作 | 送出 |
| --- | --- |
| `term_clear()` | `ESC [2J ESC [H`（清畫面、游標回左上） |
| `term_goto(col, row)` | `ESC [row+1;col+1H`（ANSI 從 1 開始數） |
| `term_set_color(fg, bg)` | `ESC [38;2;R;G;Bm ESC [48;2;R;G;Bm`（24-bit 真彩色） |
| `\b` | `\b \b`（退一格、印空白蓋掉、再退一格） |

有了這些，`LIFE` 的全螢幕動畫、`COLOR 1F`、藍色當機畫面在網頁上都會正確顯示。開機時也先送一次清畫面，把 BIOS 印在序列埠的殘字清掉。

## 網頁本身

```text
web/
  index.html            Win95 風格的視窗 + xterm.js 終端機
  mydos-boot.js         QEMU 的開機參數、把映像放進虛擬檔案系統
  coi-serviceworker.js  讓 GitHub Pages 也能用 SharedArrayBuffer（見下面）
  vendor/               xterm.js、xterm-pty
  qemu/                 make web 時下載的 QEMU Wasm
  mydos.iso.gz          make web 時產生
  disk.img.gz
```

### 開機參數

跟 `make run` 幾乎一樣，差在 `-nographic`（序列埠接到網頁）和 `-L /pack-rom/`（BIOS 檔案的位置）：

```js
Module['arguments'] = [
    '-nographic', '-M', 'pc', '-m', '256M',
    '-L', '/pack-rom/',
    '-cdrom', '/mydos/mydos.iso',
    '-drive', 'file=/mydos/disk.img,format=raw,if=ide,index=0',
    '-boot', 'd', '-rtc', 'base=localtime',
];
```

### 把映像放進去

瀏覽器裡的 QEMU 讀的是 Emscripten 的**記憶體檔案系統**，不是真的硬碟。所以在 QEMU 啟動前（`preRun`），先下載映像、寫進 `/mydos/`：

```js
Module['addRunDependency']('mydos:' + name);          // 「先別啟動，還有東西沒好」
fetch(name + '.gz')
    .then((r) => new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer())
    .then((buf) => {
        Module['FS_createDataFile']('/mydos', name, new Uint8Array(buf), true, true, true);
        Module['removeRunDependency']('mydos:' + name);   // 「好了，可以啟動」
    });
```

**為什麼要 gzip？** `disk.img` 有 32 MB，但幾乎全是 0，壓縮後只剩約 40 KB；`mydos.iso` 從 4 MB 變 1 MB。瀏覽器內建 `DecompressionStream` 可以邊下載邊解壓，不需要任何函式庫。

因為檔案系統在記憶體裡，**在網頁上建的檔案重新整理就會消失**。

### SharedArrayBuffer 與 coi-serviceworker

QEMU Wasm 用多執行緒（Web Worker），執行緒之間要共用記憶體（`SharedArrayBuffer`）。瀏覽器為了防範 Spectre 這類攻擊，只在網頁處於「跨來源隔離」狀態時才開放它，條件是伺服器要回傳兩個標頭：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

GitHub Pages 不能自訂標頭。[coi-serviceworker](https://github.com/gzuidhof/coi-serviceworker) 的做法是註冊一個 Service Worker，攔截所有請求、自己補上這兩個標頭，然後重新整理一次網頁。所以第一次打開時網頁會自動重載一次，這是正常的。

**踩到的坑：** Service Worker 只能管理**它所在資料夾以下**的網頁。一開始我把它放在 `vendor/` 裡，它就只管得到 `/vendor/`，管不到首頁，結果首頁永遠等不到隔離狀態，一直重新整理。它必須放在網站根目錄。

## GitHub Actions 自動部署

`.github/workflows/pages.yml` 在每次 push 到 `main` 時：

```text
1. 在 GitHub 的 Ubuntu 機器上裝 gcc、xorriso、dosfstools、mtools
2. make web          編核心、編程式、做映像、下載 QEMU Wasm、壓縮
3. 把 web/ 整個資料夾上傳成 Pages 的內容
4. 部署
```

QEMU Wasm 和映像都是建置時產生的，**不需要 commit 進 git**（`.gitignore` 已經排除），repo 保持乾淨。

### 第一次發佈

```bash
cd ~/mydos-1.0
./tools/publish-github-pages.sh          # 或指定 repo 名稱：./tools/publish-github-pages.sh my-os
```

腳本會：確認 GitHub CLI（`gh`）已安裝並登入 → 建立**公開** repo（免費帳號的 Pages 需要公開）→ 把 Pages 來源設成 GitHub Actions → push → 等建置完成並印出網址。

想手動設定的話：在 GitHub 建 repo、push、到 **Settings → Pages → Source** 選 **GitHub Actions**。

## 測試

我用沒有特殊標頭的普通 HTTP 伺服器（模擬 GitHub Pages）加 Chrome 實測：

| 項目 | 結果 |
| --- | --- |
| 開機到 `C:\>` | 約 12 秒（不含第一次下載 QEMU） |
| `VER`、`DIR \BIN`、`HELLO web` | 正常 |
| `CRASH`、`CRASH LOOP` + Ctrl+C | 程式被結束，系統繼續 |
| `GUESS` 中途 Ctrl+C | 立刻結束 |
| ↑ ↑ Enter | 叫回歷史指令 |
| `COPY CON` + Ctrl+Z | 存檔成功，`TYPE` 讀得回來 |
| `LIFE` | 全螢幕綠色動畫 |
| `PANIC` | 藍色當機畫面 |

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| 網頁一直重新整理 | `coi-serviceworker.js` 不在網站根目錄 |
| 停在「Loading QEMU」 | 第一次要下載 40 MB；或瀏覽器不支援（用電腦版 Chrome／Edge） |
| 打字沒反應 | 先點一下黑色畫面讓終端機取得焦點 |
| 用 `file://` 直接開 index.html 不能跑 | Service Worker 需要 http(s)，用 `make serve-web` |
| Actions 成功但網址 404 | Settings → Pages 的 Source 沒設成 GitHub Actions |

## 練習題

- [ ] 用 IndexedDB 把 `disk.img` 存起來，重新整理後檔案還在
- [ ] 網頁加一個「上傳檔案到 C 槽」的按鈕（寫進 Emscripten FS，再重開機）
- [ ] 讓 xterm.js 的欄列數跟 MYDOS 的 `term_cols()`／`term_rows()` 一致
- [ ] 進階：自己用 Emscripten 編一個有圖形輸出的 QEMU，讓 framebuffer 畫在 `<canvas>` 上
