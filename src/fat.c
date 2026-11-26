#include "fs.h"
#include "ata.h"
#include "mm.h"
#include "string.h"
#include "timer.h"

// =====================================================================
//  FAT16 磁碟長這樣（以磁區為單位）：
//
//  ┌──────┬──────────┬───────┬───────┬────────────┬─────────────────────
//  │ 開機 │ 保留磁區 │ FAT 1 │ FAT 2 │ 根目錄     │ 資料區：cluster 2, 3, 4…
//  │ 磁區 │          │       │ (備份) │ (固定大小) │
//  └──────┴──────────┴───────┴───────┴────────────┴─────────────────────
//
//  檔案內容放在資料區，以 cluster（數個磁區）為單位。
//  FAT 是一張大表：fat[n] = 這個檔案的下一個 cluster 是誰。
//  例：fat[5] = 9, fat[9] = 6, fat[6] = 0xFFFF → 檔案佔 5 → 9 → 6。
//  0 = 空的；0xFFF8 以上 = 鏈結結尾。
// =====================================================================

struct __attribute__((packed)) bpb {          // 開機磁區開頭的「BIOS 參數區」
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint8_t  drive;
    uint8_t  reserved;
    uint8_t  signature;
    uint32_t volume_id;
    char     label[11];
    char     fs_type[8];
};

struct __attribute__((packed)) dirent {       // 目錄裡的一筆，固定 32 byte
    char     name[11];        // "HELLO   EXE"：8 + 3，空白補齊，沒有點
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  ctime_tenth;
    uint16_t ctime, cdate;
    uint16_t adate;
    uint16_t cluster_hi;      // FAT32 才用
    uint16_t mtime, mdate;
    uint16_t cluster;         // 第一個 cluster
    uint32_t size;
};

#define ATTR_LFN   0x0F       // Windows 長檔名的特殊項目，我們跳過
#define END_OF_CHAIN 0xFFF8
#define DELETED    ((char)0xE5)
#define ROOT       0          // 用 cluster 0 代表根目錄

static int       mounted;
static uint32_t  part_lba;            // 分割區起點（無分割表時是 0）
static uint32_t  fat_lba, root_lba, data_lba;
static uint32_t  root_entries, sectors_per_fat, fat_count;
static uint32_t  spc, cluster_bytes, cluster_count;
static uint16_t *fat;                 // 整張 FAT 讀進記憶體
static char      label[12];

// ---------------------------------------------------------------------
//  磁區與 cluster
// ---------------------------------------------------------------------
static int disk_read(uint32_t lba, uint32_t n, void *buf)        { return ata_read(part_lba + lba, n, buf) ? FS_EIO : 0; }
static int disk_write(uint32_t lba, uint32_t n, const void *buf) { return ata_write(part_lba + lba, n, buf) ? FS_EIO : 0; }

static uint32_t cluster_lba(uint32_t c) { return data_lba + (c - 2) * spc; }
static int is_valid_cluster(uint32_t c) { return c >= 2 && c < cluster_count + 2; }

static int fat_flush(void) {
    for (uint32_t i = 0; i < fat_count; i++)       // 每一份備份都要寫
        if (disk_write(fat_lba + i * sectors_per_fat, sectors_per_fat, fat)) return FS_EIO;
    return 0;
}

static uint32_t alloc_cluster(void) {
    for (uint32_t c = 2; c < cluster_count + 2; c++) {
        if (fat[c] == 0) { fat[c] = 0xFFFF; return c; }
    }
    return 0;
}

static void free_chain(uint32_t c) {
    while (is_valid_cluster(c)) {
        uint32_t next = fat[c];
        fat[c] = 0;
        c = next;
    }
}

static int zero_cluster(uint32_t c) {
    uint8_t *z = kzalloc(cluster_bytes);
    if (!z) return FS_ENOSPC;
    int r = disk_write(cluster_lba(c), spc, z);
    kfree(z);
    return r;
}

// ---------------------------------------------------------------------
//  名稱：「hello.exe」⇄「HELLO   EXE」
// ---------------------------------------------------------------------
static int valid_char(char c) {
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return strchr("!#$%&'()-@^_`{}~", c) != 0;
}

static int to_83(const char *name, char out[11]) {
    memset(out, ' ', 11);
    if (!strcmp(name, "."))  { out[0] = '.'; return 0; }
    if (!strcmp(name, "..")) { out[0] = out[1] = '.'; return 0; }
    int i = 0, j = 0;
    for (; name[i] && name[i] != '.'; i++, j++) {
        char c = toupper(name[i]);
        if (j >= 8 || !valid_char(c)) return FS_EINVAL;
        out[j] = c;
    }
    if (j == 0) return FS_EINVAL;
    if (name[i] == '.') {
        i++;
        for (j = 8; name[i]; i++, j++) {
            char c = toupper(name[i]);
            if (j >= 11 || !valid_char(c)) return FS_EINVAL;
            out[j] = c;
        }
    }
    return 0;
}

static void from_83(const char in[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && in[i] != ' '; i++) out[n++] = in[i];
    if (in[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++) out[n++] = in[i];
    }
    out[n] = 0;
}

static void stamp(struct dirent *e) {
    struct datetime t;
    rtc_read(&t);
    e->mtime = (t.hour << 11) | (t.minute << 5) | (t.second / 2);
    e->mdate = ((t.year - 1980) << 9) | (t.month << 5) | t.day;
    e->ctime = e->mtime; e->cdate = e->adate = e->mdate;
}

static void fill_info(const struct dirent *e, struct fs_info *out) {
    from_83(e->name, out->name);
    out->attr = e->attr;
    out->size = e->size;
    out->year = (e->mdate >> 9) + 1980;
    out->month = (e->mdate >> 5) & 15;
    out->day = e->mdate & 31;
    out->hour = e->mtime >> 11;
    out->minute = (e->mtime >> 5) & 63;
}

// ---------------------------------------------------------------------
//  目錄：第 idx 筆項目在硬碟的哪個磁區、第幾個 byte
// ---------------------------------------------------------------------
static int entry_location(uint32_t dir, uint32_t idx, uint32_t *lba, uint32_t *off) {
    uint32_t byte = idx * 32;
    if (dir == ROOT) {                                 // 根目錄：固定位置、固定大小
        if (idx >= root_entries) return FS_ENOENT;
        *lba = root_lba + byte / SECTOR_SIZE;
    } else {                                           // 子目錄：沿 cluster 鏈結走
        uint32_t c = dir;
        for (uint32_t skip = byte / cluster_bytes; skip; skip--) {
            c = fat[c];
            if (!is_valid_cluster(c)) return FS_ENOENT;
        }
        *lba = cluster_lba(c) + (byte % cluster_bytes) / SECTOR_SIZE;
    }
    *off = byte % SECTOR_SIZE;
    return 0;
}

static int entry_get(uint32_t dir, uint32_t idx, struct dirent *e) {
    uint32_t lba, off;
    uint8_t sec[SECTOR_SIZE];
    int r = entry_location(dir, idx, &lba, &off);
    if (r) return r;
    if (disk_read(lba, 1, sec)) return FS_EIO;
    memcpy(e, sec + off, sizeof(*e));
    return 0;
}

static int entry_put(uint32_t dir, uint32_t idx, const struct dirent *e) {
    uint32_t lba, off;
    uint8_t sec[SECTOR_SIZE];
    int r = entry_location(dir, idx, &lba, &off);
    if (r) return r;
    if (disk_read(lba, 1, sec)) return FS_EIO;       // 讀–改–寫：一個磁區有 16 筆
    memcpy(sec + off, e, sizeof(*e));
    return disk_write(lba, 1, sec);
}

static int is_live(const struct dirent *e) {
    return e->name[0] != DELETED && e->attr != ATTR_LFN && !(e->attr & FS_ATTR_VOLUME);
}

// 在 dir 裡找 name11；找到回傳 0，並給出項目內容和索引
static int dir_find(uint32_t dir, const char name11[11], struct dirent *e, uint32_t *idx) {
    for (uint32_t i = 0;; i++) {
        int r = entry_get(dir, i, e);
        if (r == FS_ENOENT || (!r && e->name[0] == 0)) return FS_ENOENT;  // 0 = 後面都沒了
        if (r) return r;
        if (is_live(e) && !memcmp(e->name, name11, 11)) { *idx = i; return 0; }
    }
}

// 找一個空位；子目錄滿了就多接一個 cluster
static int dir_free_slot(uint32_t dir, uint32_t *idx) {
    struct dirent e;
    uint32_t i = 0;
    for (;; i++) {
        int r = entry_get(dir, i, &e);
        if (r == FS_ENOENT) break;
        if (r) return r;
        if (e.name[0] == 0 || e.name[0] == DELETED) { *idx = i; return 0; }
    }
    if (dir == ROOT) return FS_ENOSPC;                 // 根目錄大小固定，不能長
    uint32_t last = dir;
    while (is_valid_cluster(fat[last])) last = fat[last];
    uint32_t c = alloc_cluster();
    if (!c) return FS_ENOSPC;
    if (zero_cluster(c)) { fat[c] = 0; return FS_EIO; }
    fat[last] = c;
    *idx = i;
    return fat_flush();
}

// ---------------------------------------------------------------------
//  路徑
// ---------------------------------------------------------------------
// 走完整個路徑。成功時：*e 是最後一段的項目，*parent / *idx 是它在哪個目錄第幾筆。
// 路徑是根目錄本身時，*e 會是一個假的資料夾項目（cluster 0）。
static int walk(const char *path, struct dirent *e, uint32_t *parent, uint32_t *idx) {
    if (!mounted) return FS_ENOFS;
    memset(e, 0, sizeof(*e));
    e->attr = FS_ATTR_DIR;                        // 從根目錄出發
    uint32_t dir = ROOT, i = 0;
    *parent = ROOT;
    const char *p = path;
    while (*p == '\\') p++;
    while (*p) {
        char part[13], name11[11];
        int n = 0;
        while (*p && *p != '\\') { if (n < 12) part[n++] = *p; p++; }
        part[n] = 0;
        while (*p == '\\') p++;
        if (!(e->attr & FS_ATTR_DIR)) return FS_ENOTDIR;
        if (to_83(part, name11)) return FS_EINVAL;
        int r = dir_find(dir, name11, e, &i);
        if (r) return r;
        *parent = dir;
        dir = e->cluster;                         // ".." 指回根目錄時 cluster 也是 0
    }
    *idx = i;
    return 0;
}

// 把「\A\B\FILE.TXT」拆成「\A\B」和「FILE.TXT」，並確認父目錄存在
static int parent_of(const char *path, uint32_t *dir, char name11[11]) {
    char parent[FS_MAX_PATH];
    const char *slash = strrchr(path, '\\');
    const char *leaf = slash ? slash + 1 : path;
    size_t plen = slash ? (size_t)(slash - path) : 0;
    if (plen >= sizeof(parent)) return FS_EINVAL;
    memcpy(parent, path, plen);
    parent[plen] = 0;
    if (to_83(leaf, name11) || name11[0] == '.') return FS_EINVAL;
    struct dirent e;
    uint32_t pd, idx;
    int r = walk(parent, &e, &pd, &idx);
    if (r) return r;
    if (!(e.attr & FS_ATTR_DIR)) return FS_ENOTDIR;
    *dir = e.cluster;
    return 0;
}

// ---------------------------------------------------------------------
//  對外介面
// ---------------------------------------------------------------------
int fs_mount(void) {
    uint8_t sec[SECTOR_SIZE];
    if (ata_read(0, 1, sec)) return FS_EIO;
    struct bpb *b = (struct bpb *)sec;

    // 有分割表（MBR）的話，跳到第一個 FAT16 分割區
    if (b->bytes_per_sector != 512 && sec[510] == 0x55 && sec[511] == 0xAA) {
        uint8_t type = sec[446 + 4];
        if (type != 0x04 && type != 0x06 && type != 0x0E) return FS_ENOFS;
        part_lba = *(uint32_t *)(sec + 446 + 8);
        if (ata_read(part_lba, 1, sec)) return FS_EIO;
    }
    if (b->bytes_per_sector != 512 || !b->sectors_per_cluster || !b->sectors_per_fat)
        return FS_ENOFS;

    spc             = b->sectors_per_cluster;
    cluster_bytes   = spc * SECTOR_SIZE;
    fat_count       = b->fat_count;
    sectors_per_fat = b->sectors_per_fat;
    root_entries    = b->root_entries;
    fat_lba         = b->reserved_sectors;
    root_lba        = fat_lba + fat_count * sectors_per_fat;
    data_lba        = root_lba + (root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t total  = b->total_sectors16 ? b->total_sectors16 : b->total_sectors32;
    cluster_count   = (total - data_lba) / spc;
    if (cluster_count < 4085 || cluster_count >= 65525) return FS_ENOFS;   // 不是 FAT16

    memcpy(label, b->label, 11);
    for (int i = 10; i >= 0 && label[i] == ' '; i--) label[i] = 0;

    fat = kmalloc(sectors_per_fat * SECTOR_SIZE);
    if (!fat) return FS_ENOSPC;
    if (disk_read(fat_lba, sectors_per_fat, fat)) return FS_EIO;
    mounted = 1;
    return 0;
}

int fs_stat(const char *path, struct fs_info *out) {
    struct dirent e;
    uint32_t parent, idx;
    int r = walk(path, &e, &parent, &idx);
    if (r) return r;
    if (e.name[0] == 0) {                     // 根目錄
        memset(out, 0, sizeof(*out));
        strcpy(out->name, "\\");
        out->attr = FS_ATTR_DIR;
        return 0;
    }
    fill_info(&e, out);
    return 0;
}

int fs_readdir(const char *dir, int *cookie, struct fs_info *out) {
    struct dirent e;
    uint32_t parent, idx;
    int r = walk(dir, &e, &parent, &idx);
    if (r) return r;
    if (!(e.attr & FS_ATTR_DIR)) return FS_ENOTDIR;
    uint32_t cl = e.cluster;
    for (uint32_t i = *cookie;; i++) {
        r = entry_get(cl, i, &e);
        if (r == FS_ENOENT || (!r && e.name[0] == 0)) return FS_ENOENT;
        if (r) return r;
        if (!is_live(&e)) continue;
        fill_info(&e, out);
        *cookie = i + 1;
        return 0;
    }
}

int fs_read_file(const char *path, void **data, uint32_t *size) {
    struct dirent e;
    uint32_t parent, idx;
    int r = walk(path, &e, &parent, &idx);
    if (r) return r;
    if (e.attr & FS_ATTR_DIR) return FS_EISDIR;

    // 多配到 cluster 的整數倍，最後一個 cluster 可以整塊讀進來
    uint32_t alloc = ALIGN_UP(e.size, cluster_bytes) + 1;
    uint8_t *buf = kmalloc(alloc);
    if (!buf) return FS_ENOSPC;
    uint32_t done = 0;
    for (uint32_t c = e.cluster; done < e.size; c = fat[c]) {
        if (!is_valid_cluster(c) || disk_read(cluster_lba(c), spc, buf + done)) {
            kfree(buf);
            return FS_EIO;
        }
        done += cluster_bytes;
    }
    buf[e.size] = 0;                          // 方便當字串用
    *data = buf;
    *size = e.size;
    return 0;
}

int fs_write_file(const char *path, const void *data, uint32_t size) {
    struct dirent e;
    uint32_t dir, idx;
    char name11[11];
    int r = walk(path, &e, &dir, &idx);
    if (r == 0) {                             // 已存在：先把舊內容的 cluster 還回去
        if (e.attr & FS_ATTR_DIR) return FS_EISDIR;
        if (e.attr & FS_ATTR_READONLY) return FS_EEXIST;
        free_chain(e.cluster);
    } else if (r == FS_ENOENT) {              // 新檔案：在父目錄找空位
        if ((r = parent_of(path, &dir, name11))) return r;
        if ((r = dir_free_slot(dir, &idx))) return r;
        memset(&e, 0, sizeof(e));
        memcpy(e.name, name11, 11);
        e.attr = FS_ATTR_ARCHIVE;
    } else {
        return r;
    }

    // 一個 cluster 一個 cluster 配，邊配邊寫
    uint32_t first = 0, prev = 0;
    const uint8_t *src = data;
    uint8_t *tmp = kzalloc(cluster_bytes);
    if (!tmp) return FS_ENOSPC;
    for (uint32_t done = 0; done < size; done += cluster_bytes) {
        uint32_t c = alloc_cluster();
        if (!c) { free_chain(first); kfree(tmp); fat_flush(); return FS_ENOSPC; }
        if (prev) fat[prev] = c; else first = c;
        prev = c;
        uint32_t n = size - done < cluster_bytes ? size - done : cluster_bytes;
        memset(tmp, 0, cluster_bytes);
        memcpy(tmp, src + done, n);
        if (disk_write(cluster_lba(c), spc, tmp)) { kfree(tmp); return FS_EIO; }
    }
    kfree(tmp);

    e.cluster = first;
    e.size = size;
    stamp(&e);
    if ((r = fat_flush())) return r;          // 先寫 FAT 再寫目錄項目
    return entry_put(dir, idx, &e);
}

// 刪掉一筆項目，連同它前面的長檔名項目
static int erase_entry(uint32_t dir, uint32_t idx) {
    struct dirent e;
    int r = entry_get(dir, idx, &e);
    if (r) return r;
    e.name[0] = DELETED;
    if ((r = entry_put(dir, idx, &e))) return r;
    while (idx-- > 0) {
        if (entry_get(dir, idx, &e) || e.attr != ATTR_LFN || e.name[0] == DELETED) break;
        e.name[0] = DELETED;
        entry_put(dir, idx, &e);
    }
    return 0;
}

int fs_delete(const char *path) {
    struct dirent e;
    uint32_t dir, idx;
    int r = walk(path, &e, &dir, &idx);
    if (r) return r;
    if (e.attr & FS_ATTR_DIR) return FS_EISDIR;
    if (e.attr & FS_ATTR_READONLY) return FS_EEXIST;
    free_chain(e.cluster);
    if ((r = fat_flush())) return r;
    return erase_entry(dir, idx);
}

int fs_mkdir(const char *path) {
    struct dirent e;
    uint32_t dir, idx;
    char name11[11];
    int r = walk(path, &e, &dir, &idx);
    if (r == 0) return FS_EEXIST;
    if (r != FS_ENOENT) return r;
    if ((r = parent_of(path, &dir, name11))) return r;
    if ((r = dir_free_slot(dir, &idx))) return r;

    uint32_t c = alloc_cluster();
    if (!c) return FS_ENOSPC;
    if ((r = zero_cluster(c))) { fat[c] = 0; return r; }

    // 每個子目錄開頭都有 . (自己) 和 .. (上一層)
    struct dirent dot;
    memset(&dot, 0, sizeof(dot));
    memset(dot.name, ' ', 11);
    dot.name[0] = '.';
    dot.attr = FS_ATTR_DIR;
    dot.cluster = c;
    stamp(&dot);
    entry_put(c, 0, &dot);
    dot.name[1] = '.';
    dot.cluster = dir;
    entry_put(c, 1, &dot);

    memset(&e, 0, sizeof(e));
    memcpy(e.name, name11, 11);
    e.attr = FS_ATTR_DIR;
    e.cluster = c;
    stamp(&e);
    if ((r = fat_flush())) return r;
    return entry_put(dir, idx, &e);
}

int fs_rmdir(const char *path) {
    struct dirent e, sub;
    uint32_t dir, idx;
    int r = walk(path, &e, &dir, &idx);
    if (r) return r;
    if (!(e.attr & FS_ATTR_DIR)) return FS_ENOTDIR;
    if (e.name[0] == 0) return FS_EINVAL;                 // 不能刪根目錄
    for (uint32_t i = 0;; i++) {                          // 除了 . 和 .. 不能有別的
        r = entry_get(e.cluster, i, &sub);
        if (r == FS_ENOENT || (!r && sub.name[0] == 0)) break;
        if (r) return r;
        if (is_live(&sub) && sub.name[0] != '.') return FS_ENOTEMPTY;
    }
    free_chain(e.cluster);
    if ((r = fat_flush())) return r;
    return erase_entry(dir, idx);
}

int fs_rename(const char *path, const char *new_name) {
    struct dirent e, other;
    uint32_t dir, idx, oidx;
    char name11[11];
    int r = walk(path, &e, &dir, &idx);
    if (r) return r;
    if (e.name[0] == 0) return FS_EINVAL;
    if (to_83(new_name, name11) || name11[0] == '.') return FS_EINVAL;
    if (!dir_find(dir, name11, &other, &oidx)) return FS_EEXIST;
    memcpy(e.name, name11, 11);
    return entry_put(dir, idx, &e);
}

uint64_t fs_free_bytes(void) {
    if (!mounted) return 0;
    uint64_t n = 0;
    for (uint32_t c = 2; c < cluster_count + 2; c++) if (fat[c] == 0) n++;
    return n * cluster_bytes;
}

uint64_t fs_total_bytes(void) { return mounted ? (uint64_t)cluster_count * cluster_bytes : 0; }
const char *fs_label(void) { return label[0] ? label : "NO NAME"; }

const char *fs_strerror(int err) {
    switch (err) {
    case FS_ENOENT:    return "File not found";
    case FS_EEXIST:    return "File exists or is read-only";
    case FS_ENOTDIR:   return "Path not found";
    case FS_EISDIR:    return "Is a directory";
    case FS_ENOSPC:    return "Insufficient disk space";
    case FS_EIO:       return "Disk read/write error";
    case FS_EINVAL:    return "Invalid file name";
    case FS_ENOTEMPTY: return "Directory not empty";
    case FS_ENOFS:     return "No disk";
    default:           return "Unknown error";
    }
}
