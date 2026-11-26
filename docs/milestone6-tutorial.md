# MYDOS 教學：里程碑 6 — 硬碟與 FAT16 檔案系統

## 目標與成果

這是讓 MYDOS 真正像 DOS 的里程碑：有一顆 C 槽，可以列目錄、看檔案、建資料夾、複製、刪除。完成後版本號是 v0.6。

完成時你會看到：

- 開機訊息 `[ ok ] Disk: QEMU HARDDISK, 32 MB` 和 `[ ok ] Drive C: FAT16 "MYDOS" mounted`
- 提示字元會顯示目前目錄：`C:\DOCS>`
- 新指令：`DIR` `CD` `MD` `RD` `TYPE` `COPY` `DEL` `REN` `VOL` `COLOR`
- `COPY CON 檔名`：直接用鍵盤打字存檔，Ctrl+Z 再 Enter 結束（DOS 經典用法）
- 萬用字元：`DIR *.TXT`、`DEL *.*`、`COPY *.TXT SUB`

| 層 | 檔案 | 職責 |
| --- | --- | --- |
| 命令列 | `shell.c`、`cmd_fs.c` | 目前目錄、相對路徑、萬用字元、DOS 風格輸出 |
| 檔案系統 | `fat.c`（介面在 `fs.h`） | 檔名 → 磁區 |
| 磁碟驅動 | `ata.c` | 讀寫第 N 個磁區 |

上層只透過 `fs.h` 跟檔案系統說話，完全不知道底下是 FAT16。以後要換成別的檔案系統，命令列不用改。

## 硬碟映像

`make` 會用 Linux 的工具做出 `disk.img`：

```makefile
disk.img: user-programs $(shell find disk -type f)
	mkfs.fat -F 16 -n MYDOS -C $@ 32768     # 32 MB、FAT16、標籤 MYDOS
	mcopy -s -i $@ disk/* ::/               # 把 disk/ 資料夾的內容複製進去
```

`disk/` 資料夾就是 C 槽的「原始內容」。想放什麼檔案進 MYDOS，丟進 `disk/` 再 `make` 就好。需要安裝 `dosfstools` 和 `mtools`。

**注意：** `disk/` 有變動時 `make` 會重做 `disk.img`，你在 MYDOS 裡新增的檔案會消失。想保留就先用 `mcopy -i disk.img ::/檔名 .` 拿出來。

QEMU 用 `-drive file=disk.img,format=raw,if=ide,index=0` 把它接成第一個 IDE 通道的主碟（光碟機在第二通道，不衝突）。

## ATA PIO：一次搬 2 byte

最簡單的硬碟介面是 **PIO**（Programmed I/O）：CPU 自己一個字一個字地從 port 搬資料。慢，但不需要 DMA、不需要中斷，幾十行就寫完。

主通道的 port：

| port | 讀 | 寫 |
| --- | --- | --- |
| `0x1F0` | 資料（16 bit） | 資料 |
| `0x1F2` | | 磁區數 |
| `0x1F3–0x1F5` | | LBA bit 0–23 |
| `0x1F6` | | `0xE0` \| LBA bit 24–27（LBA 模式、主碟） |
| `0x1F7` | 狀態 | 命令 |

**LBA**（Logical Block Address）就是「第幾個磁區」，從 0 開始。一個磁區 512 byte。

讀一個磁區的流程：

```text
1. 等 BSY 清掉
2. 寫磁區數、LBA、0xE0|LBA高位、命令 0x20（READ）
3. 等 BSY 清掉、DRQ 亮起
4. 從 0x1F0 讀 256 次 inw → 512 byte
```

寫入是命令 `0x30` 加 `outw`，最後送 `0xE7`（FLUSH）讓硬碟把內部快取寫進碟片。

狀態位元：

| bit | 名稱 | 意思 |
| --- | --- | --- |
| 7 | BSY | 忙碌中，**其他位元都不可信** |
| 3 | DRQ | 資料準備好了 |
| 5 | DF | 硬碟故障 |
| 0 | ERR | 命令失敗 |

送完命令後要讀 4 次控制暫存器（約 400 ns）再看狀態，因為硬碟需要一點時間才會把 BSY 拉起來。

`ata_init()` 送 IDENTIFY（`0xEC`）確認硬碟存在，順便讀出型號和容量。型號字串每 2 byte 前後顛倒（ATA 規格的歷史包袱），要交換回來。

## FAT16：磁碟上的地圖

### 整體配置

```text
┌──────┬──────────┬───────┬───────┬────────────┬──────────────────────
│ 開機 │ 保留磁區 │ FAT 1 │ FAT 2 │ 根目錄     │ 資料區：cluster 2, 3, 4…
│ 磁區 │          │       │ (備份) │ (固定大小) │
└──────┴──────────┴───────┴───────┴────────────┴──────────────────────
```

**開機磁區**開頭是 BPB（BIOS Parameter Block），記錄每個區域多大。`fs_mount()` 讀它、算出各區起點：

```c
fat_lba  = reserved_sectors;
root_lba = fat_lba + fat_count * sectors_per_fat;
data_lba = root_lba + root_entries * 32 / 512;
```

**cluster** 是資料區的配置單位，由好幾個磁區組成（32 MB 的磁碟通常是 4 個 = 2 KB）。一個 3 byte 的檔案也要佔一整個 cluster。

FAT12、16、32 的差別只在 FAT 每格幾 bit。判斷方式是**cluster 總數**：4085 以下是 FAT12，65525 以上是 FAT32，中間是 FAT16。

### FAT：一張「下一站」表

FAT 是一個大陣列，`fat[n]` 代表「cluster n 的下一個 cluster 是誰」：

```text
fat[5] = 9    fat[9] = 6    fat[6] = 0xFFFF
→ 這個檔案依序佔 cluster 5 → 9 → 6
```

| 值 | 意思 |
| --- | --- |
| `0` | 空的 |
| `0xFFF8` 以上 | 鏈結結尾 |
| 其他 | 下一個 cluster |

32 MB 磁碟的 FAT 才 64 KB，MYDOS 掛載時整張讀進記憶體（`kmalloc`），改完再寫回兩份。

### 目錄：32 byte 一筆

目錄本身也只是一串固定格式的項目：

```c
struct dirent {
    char     name[11];     // "HELLO   EXE"：8 + 3，空白補齊，沒有點
    uint8_t  attr;         // 0x10 = 資料夾、0x01 = 唯讀……
    …
    uint16_t mtime, mdate; // 修改時間
    uint16_t cluster;      // 第一個 cluster
    uint32_t size;         // byte 數
};
```

`name[0]` 有特殊意義：`0x00` = 這筆和後面全都沒用過；`0xE5` = 已刪除。

**根目錄**位置和大小固定（通常 512 筆）。**子目錄**則跟檔案一樣放在資料區，用 cluster 鏈結，可以長大。每個子目錄開頭固定有 `.`（指向自己）和 `..`（指向上一層，根目錄是 cluster 0）。

**時間格式**塞在 16 bit 裡：`時<<11 | 分<<5 | 秒/2`、`(年-1980)<<9 | 月<<5 | 日`。秒只有偶數精度。

### 8.3 檔名

`to_83("hello.exe")` → `"HELLO   EXE"`：轉大寫、主檔名最多 8 字、副檔名最多 3 字、只允許特定字元。`from_83` 反過來。不合法的名字一律回 `FS_EINVAL`。

Windows 的長檔名會在 8.3 項目前面多塞幾筆 `attr == 0x0F` 的特殊項目。MYDOS 讀的時候跳過它們，刪除時把它們一起標成已刪除，避免留下孤兒。

## fat.c 的設計

### 一律用完整路徑

`fs.h` 的所有函式只收 `\DOCS\NOTES.TXT` 這種絕對路徑。相對路徑、`.`、`..`、`C:` 都由命令列的 `path_resolve()` 先換算好。這讓檔案系統很單純：從根目錄一段一段往下找就好（`walk()`）。

### 讀寫目錄項目

```c
static int entry_location(uint32_t dir, uint32_t idx, uint32_t *lba, uint32_t *off);
static int entry_get(uint32_t dir, uint32_t idx, struct dirent *e);
static int entry_put(uint32_t dir, uint32_t idx, const struct dirent *e);
```

「第 idx 筆在哪個磁區的第幾個 byte」是唯一需要分辨根目錄和子目錄的地方，其他程式都只用 `entry_get/put`。`entry_put` 是**讀–改–寫**：一個磁區有 16 筆，只改其中一筆也要整個磁區讀出來、改、寫回去。

### 寫檔的順序

```text
1. 已存在 → 把舊的 cluster 鏈還回 FAT；不存在 → 在父目錄找空位
2. 一個 cluster 一個 cluster：配置、串起來、寫資料
3. 寫 FAT（兩份）
4. 最後才寫目錄項目
```

先寫 FAT 再寫目錄項目，是為了**斷電時損失最小**：如果在第 3、4 步之間斷電，最多是幾個 cluster 被標成使用中卻沒人指向（`fsck` 會回收），不會出現「目錄說檔案在 cluster 5，cluster 5 卻被別人拿去用」這種會毀掉資料的狀況。

### 目錄滿了

子目錄找不到空位時，`dir_free_slot()` 會配一個新的 cluster、清成 0、接在目錄鏈結尾端。根目錄大小固定，滿了就是滿了（DOS 也一樣）。

## 命令列

### 目前目錄與路徑換算

`shell_cwd` 存目前目錄（`"\"` 或 `"\DOCS\SUB"`）。`path_resolve()` 的做法：

1. 去掉開頭的 `C:`
2. 不是 `\` 開頭就接在 `shell_cwd` 後面
3. 一段一段處理：`.` 跳過、`..` 刪掉最後一段、其他的轉大寫接上去

```text
目前在 \DOCS
  "sub\a.txt"     → \DOCS\SUB\A.TXT
  "..\readme.txt" → \README.TXT
  "\bin"          → \BIN
  "..\..\.."      → \（不會跑到根目錄外面）
```

DOS 也接受 `CD..`、`CD\` 這種沒空白的寫法，`shell_execute` 特別處理。

### 萬用字元

```c
int wildcard_match(const char *pat, const char *name);
```

遞迴實作：`*` 試著吃掉 0、1、2……個字元，看剩下的能不能配對；`?` 剛好吃一個。另外照 DOS 習慣，`*.*` 比對所有檔案，`README.*` 也比對沒有副檔名的 `README`。

`DEL *.TXT` 要注意：**不能邊讀目錄邊刪除**，刪掉一筆會改變後面的狀態。所以先把符合的檔名全部收集起來，再一個一個刪。`DEL *.*` 會像 DOS 一樣問 `Are you sure (Y/N)?`。

### COPY CON

```text
C:\>COPY CON HELLO.TXT
Hi from MYDOS
line two^Z
        1 file(s) copied
```

從鍵盤讀字元存進一個會自動長大的緩衝區（滿了就配兩倍大、複製過去）。Ctrl+Z（ASCII 26）是 DOS 的「檔案結尾」記號，看到 `^Z` 後按 Enter 就存檔。

## 驗證

光是在 MYDOS 裡看起來對還不夠，要確認寫出來的磁碟格式**真的正確**。做法是在 MYDOS 裡做一連串操作後關掉 QEMU，用 Linux 的工具從外面檢查：

```bash
fsck.fat -n disk.img          # 檢查 FAT 一致性，-n 只看不修
mdir -i disk.img ::/DOCS      # 用 mtools 列目錄
mtype -i disk.img ::/DOCS/B2.LOG
```

我測試時做了：建檔、複製（含萬用字元）、改名、刪除、刪資料夾，以及在一個資料夾裡建 70 個子資料夾（讓目錄長到第二個 cluster）。`fsck.fat` 回報沒有錯誤，`mtools` 讀到的內容跟 MYDOS 一致。

| 測試 | 預期 |
| --- | --- |
| `DIR` | 欄位對齊、有日期、最後兩行是檔案數和剩餘空間 |
| `CD DOCS` → `CD ..` | 提示字元跟著變 |
| `TYPE README.TXT` | 印出內容 |
| `COPY CON A.TXT` | Ctrl+Z、Enter 後 `1 file(s) copied` |
| `MD SUB` → `COPY *.TXT SUB` | 列出複製的檔名 |
| `DEL SUB\*.*` → `RD SUB` | 確認後刪除，資料夾刪得掉 |
| `REN A.TXT B.TXT` | 改名 |
| 關掉 QEMU 後 `fsck.fat -n disk.img` | 沒有錯誤 |

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| `[FAIL] No hard disk` | QEMU 沒加 `-drive … if=ide,index=0` |
| `[FAIL] Drive C: No disk` | 映像不是 FAT16（太小會變 FAT12），用 `mkfs.fat -F 16` |
| 讀出來的檔案內容錯位 | `data_lba` 算錯（根目錄的磁區數要無條件進位） |
| 新增的檔案重新 `make` 後不見 | `disk.img` 被重做了，見上面的注意事項 |
| `fsck` 說有遺失的 cluster | 寫入流程中斷，或 `free_chain` 沒呼叫 |

## 練習題

- [ ] `TYPE` 遇到超過一頁的檔案時暫停（`-- More --`），按鍵繼續
- [ ] `DIR /W`：只列檔名，一行五個
- [ ] `ATTRIB +R 檔案`：設定唯讀（`fs_write_file` 已經會拒絕覆蓋唯讀檔）
- [ ] `MOVE`：在同一顆磁碟上搬檔案只需要搬目錄項目，不用複製資料
- [ ] 進階：寫一個磁區快取（例如最近用過的 64 個磁區），`DIR` 大資料夾會快很多
