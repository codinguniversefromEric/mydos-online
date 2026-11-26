# MYDOS 教學：從零做出會開機的作業系統

> **MYDOS 1.0 的更新**
>
> - 這份教學記錄的是里程碑 1 當時的寫法：所有程式都在一個約 150 行的 `main.c` 裡。1.0 版已經拆成 `term.c`（畫字）、`serial.c`（序列埠）、`io.h`（`outb`/`inb`）、`string.c`（`memcpy` 等）這些檔案，觀念完全一樣，只是位置不同。
> - 第 5 章的 `mydos.tar.gz` 是當時的發佈方式；現在直接 `git clone` 這個 repo，再執行 `make run`。
> - 2.3 節的套件清單要多加 `dosfstools`（里程碑 6 做硬碟映像用）：`sudo apt install -y build-essential git qemu-system-x86 xorriso mtools dosfstools`
> - 不想裝任何東西，也可以直接[在瀏覽器裡開機](https://codinguniversefromeric.github.io/mydos-online/)。

## 前言

跟著這份教學做完，你會在 Windows 上用 C 寫出自己的核心，讓它在模擬器裡開機，畫出黑底畫面、系統名稱和 `C:\>`。這是 MYDOS 的里程碑 1。

MYDOS 是 DOS 風格的硬核文字介面作業系統，為人類與 AI 共同設計：

1. **透明**：AI 做的每件事都化為看得見的指令，由人類確認後才執行。
2. **可讀**：每個指令都附結構化說明，人和 AI 讀同一份。
3. **可逆**：所有改動都有紀錄，隨時可以復原。
4. **受控**：權限由核心強制執行，AI 再強也繞不過。
5. **自足**：最終能完全離線，在自己的核心裡跑 AI。

指令用英文（`DIR`、`TYPE`），系統訊息與 AI 對話用繁體中文，內部一律 UTF-8。

**需要的背景**：會 C，懂指標、struct、記憶體配置。不需要會寫組合語言。

---

## 第 1 章 觀念：在一個 OS 上做出另一個 OS

你的 OS 不是跑在 Windows「裡面」，而是跑在 **QEMU** 模擬出來的空電腦上。

1. 在 WSL（Windows 裡的 Linux）寫 C 程式碼。
2. 用 gcc 編譯成不依賴任何作業系統的核心檔。
3. 把核心和開機程式 **Limine** 打包成 `.iso`。
4. QEMU 從 `.iso` 開機：Limine 設定好 CPU，再跳進你的 `kmain()`。

開發循環：改程式碼 → `make run` → 看 QEMU 畫面 → 關掉再改。

| 平常寫程式 | 寫自己的 OS |
| --- | --- |
| `printf("Hello")` | 沒有，要自己把像素寫進螢幕記憶體 |
| `malloc()` | 沒有，要自己寫記憶體配置器 |
| `fopen()` | 沒有，要自己寫磁碟驅動和檔案系統 |
| 程式當掉，OS 幫你收拾 | 整台虛擬電腦當掉，重開 QEMU 就好 |

**為什麼用 Limine**：它會把 CPU 切到 64 位元模式、設好堆疊，直接跳進 C 函式，所以開機那段組合語言不用寫。它還會給你一塊 **framebuffer**，也就是螢幕像素對應的記憶體位址。

---

## 第 2 章 在 Windows 準備開發環境

### 2.1 安裝 WSL2

用**系統管理員身分**開 PowerShell：

```powershell
wsl --install
```

重開機後設定 Ubuntu 的使用者名稱和密碼。

### 2.2 排解：REGDB_E_CLASSNOTREG

如果出現 `Error code: Wsl/CallMsi/Install/REGDB_E_CLASSNOTREG`，改用手動安裝：

1. 到 https://github.com/microsoft/WSL/releases 下載最新的 `wsl.x.x.x.0.x64.msi` 並安裝（ARM 電腦用 `arm64.msi`）。
2. 系統管理員 PowerShell：
   ```powershell
   dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
   dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
   ```
3. 重開機後：
   ```powershell
   wsl --set-default-version 2
   wsl --install -d Ubuntu
   ```

出現「虛擬化」相關錯誤時，要到 BIOS 開啟 VT-x（Intel）或 SVM（AMD）。

### 2.3 安裝開發工具

從開始選單直接點「Ubuntu」開啟：

```bash
sudo apt update
sudo apt install -y build-essential git qemu-system-x86 xorriso mtools
```

| 工具 | 用途 |
| --- | --- |
| `build-essential` | gcc、make |
| `git` | 版本控制、下載 Limine |
| `qemu-system-x86` | 模擬電腦 |
| `xorriso`、`mtools` | 打包開機映像檔 |

### 2.4 確認圖形視窗可用

```bash
sudo apt install -y x11-apps
xeyes
```

跳出一雙眼睛就沒問題。沒出現就先執行 `wsl --update`。

### 2.5 編輯器（建議）

裝 VS Code 和 WSL 擴充套件，在專案資料夾輸入 `code .`。

---

## 第 3 章 專案結構

```
mydos/
├── src/
│   ├── main.c             核心本體（約 150 行）
│   └── font8x8_basic.h    公有領域 8×8 點陣字型
├── linker.ld              連結器腳本
├── limine.conf            開機設定
├── Makefile               建置腳本
└── README.md              理念與進度
```

第一次 `make` 會自動下載 `limine/` 和 `limine-protocol/`。

- **font8x8_basic.h**：ASCII 0–127，每字 8 byte，一個 byte 一列，最低位元在最左邊。
- **linker.ld**：`ENTRY(kmain)` 指定進入點；核心放在 `0xffffffff80000000`（Limine 規定的最高 2GB）；`.limine_requests` 區段排最前面。
- **limine.conf**：`timeout: 0` 不顯示選單，`path` 是核心在映像檔裡的位置。
- **Makefile**：`make` 建置、`make run` 開機、`make clean` 清除。

和一般程式不同的編譯參數：

- `-ffreestanding`、`-nostdlib`：沒有標準函式庫。
- `-mno-sse -mno-mmx -mno-80387`：核心還沒初始化浮點單元。
- `-mno-red-zone`：避免中斷覆蓋堆疊資料。
- `-mcmodel=kernel`：配合最高 2GB 的位址。

---

## 第 4 章 逐段讀懂 main.c

### 4.1 跟 Limine 說「我需要什麼」

```c
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};
```

核心和 Limine 用「填表」溝通：在特定區段放帶 ID 的 struct，Limine 開機時把答案填進 `response`。`used` 防止被編譯器刪掉，`volatile` 表示會被外部改寫。`LIMINE_BASE_REVISION(6)` 聲明協定版本，start/end marker 標出請求區的範圍。

### 4.2 自己寫 memcpy 等函式

gcc 可能偷偷呼叫 `memcpy`、`memset`、`memmove`、`memcmp`，沒有標準函式庫就得自己實作，否則連結失敗。

### 4.3 唯一的組合語言

```c
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
```

C 碰不到 I/O 埠，所以用一行 inline asm 包成函式。`inb` 是讀取，`halt_forever` 用 `cli; hlt` 讓 CPU 待機。

### 4.4 序列埠 COM1

7 次 `outb` 設定好序列埠。`make run` 用 `-serial stdio` 把它接到終端機。現在是除錯 log，之後是和外部 AI 溝通的通道。

### 4.5 在 framebuffer 上畫字

```c
static void put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    volatile uint32_t *p = (volatile uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
    p[x] = color;
}
```

每個像素 4 byte（`0x00RRGGBB`）。一列長度要用 `pitch`，不能用 `width * 4`，因為顯示卡可能在列尾留空白。

1. `fill_rect`：填滿矩形。
2. `draw_char`：字型位元為 1 的地方畫 2×2 方塊，每字 16×16 像素。
3. `draw_text`：逐字呼叫 `draw_char`。

### 4.6 kmain

1. 初始化序列埠，印出 `[MYDOS] kernel started`。
2. 檢查 Limine 協定版本。
3. 檢查有拿到 framebuffer。
4. 畫黑底、藍綠色狀態列、文字、`C:\>` 和游標。
5. `halt_forever()`：核心沒有「結束」可回去。

改系統名稱：改 `OS_NAME`。

---

## 第 5 章 建置與開機

```bash
cd ~
explorer.exe .          # 把 mydos.tar.gz 拖進去
tar xzf mydos.tar.gz
cd mydos
make run
```

專案放在 `/home/<你的名字>/`，不要放在 `/mnt/c/...`（慢且權限容易出問題）。

**預期結果**：QEMU 視窗出現黑底、`MYDOS v0.1` 狀態列、`C:\>`；Ubuntu 視窗印出 `[MYDOS] kernel started`。

| 症狀 | 處理 |
| --- | --- |
| `make` / `gcc: not found` | 回到 2.3 |
| `xorriso: not found` | 補裝 `xorriso mtools` |
| QEMU 視窗沒出現 | 回到 2.4 |
| 下載 Limine 失敗 | 刪掉 `limine/`、`limine-protocol/` 重跑 |
| 終端機印 `no framebuffer` | Limine 沒給圖形畫面，貼訊息檢查 |
| 終端機什麼都沒印 | 檢查 `limine.conf` 的 `path` |

---

## 第 6 章 用 Git 保存進度

```bash
cd ~/mydos
pwd                     # 確認是 /home/<你的名字>/mydos

git config --global user.name "你的名字"
git config --global user.email "你的email"
git config --global init.defaultBranch main

git init
printf "build/\nlimine/\nlimine-protocol/\n*.iso\n" > .gitignore
git add .
git commit -m "Milestone 1: first boot"
```

### 教訓：不小心在 system32 建了 repo

從系統管理員 PowerShell 進 Ubuntu，起始位置會是 `/mnt/c/WINDOWS/system32`。在那裡 `git init` 會在系統資料夾建 repo。系統檔案不會被改動，清理方式：

1. Ctrl+C 停止。
2. 確認：`ls -ld /mnt/c/WINDOWS/system32/.git /mnt/c/WINDOWS/system32/.gitignore`
3. 完整路徑刪除（不要用萬用字元）：
   ```bash
   rm -rf /mnt/c/WINDOWS/system32/.git
   rm -f /mnt/c/WINDOWS/system32/.gitignore
   ```

**預防**：從開始選單點「Ubuntu」開啟，下指令前先看提示字元的路徑。

---

## 第 7 章 路線圖與下一步

| 階段 | 時間 | 內容 |
| --- | --- | --- |
| 1 地基 | 2–3 個月 | 開機 ✅ → 文字終端 → 中斷與鍵盤 → 記憶體配置 → 命令列 |
| 2 檔案系統 | 2–3 個月 | initrd → 磁碟驅動 → FAT 讀取（DIR、TYPE）→ FAT 寫入 |
| 3 AI 住進來 v1 | 1 個月 | 序列埠橋接外部 AI；`ASK` 提出指令，按 Y 才執行 |
| 4 給 AI 讀的系統 | 2 個月 | 指令附結構化說明、可解析輸出、動作紀錄 |
| 5 可逆與受控 | 3–6 個月 | 版本化檔案系統與 UNDO；user mode、system call、分頁 |
| 6 離線 AI | 2–3 個月 | 浮點運算、自寫數學函式、移植 llama2.c |
| 7 硬核進階 | 無終點 | 網路堆疊、實體舊電腦開機、Win95 風格圖形彩蛋 |

### 下一步：里程碑 2，文字終端

- 記住游標位置，自動往後印。
- `\n` 換行，超出右邊自動折行。
- 到底部時整個畫面往上捲。
- 實作 `kprintf`，支援 `%s`、`%d`、`%x`。

每完成一個里程碑：截圖、README 打勾、`git commit`。

---

## 參考資源

- OSDev Wiki：https://wiki.osdev.org
- Limine：https://github.com/limine-bootloader/limine
- Limine 協定：https://github.com/limine-bootloader/limine-protocol
- font8x8：https://github.com/dhepper/font8x8
- WSL 發布頁面：https://github.com/microsoft/WSL/releases
- OSTEP：https://pages.cs.wisc.edu/~remzi/OSTEP/
- xv6：https://github.com/mit-pdos/xv6-public
- SerenityOS：https://github.com/SerenityOS/serenity
- llama2.c：https://github.com/karpathy/llama2.c
- 成大「Linux 核心設計」：https://wiki.csie.ncku.edu.tw/linux/schedule
