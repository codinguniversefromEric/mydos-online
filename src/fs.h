#pragma once
#include <stdint.h>

// MYDOS 的檔案系統介面（目前底下是 FAT16，見 fat.c）。
//
// 所有路徑都是「完整絕對路徑」，用反斜線分隔，例如 \BIN\HELLO.EXE。
// 相對路徑、. 和 .. 由命令列先換算好（shell.c 的 path_resolve）。
// 檔名是 DOS 的 8.3 格式，不分大小寫。

#define FS_MAX_PATH 128

#define FS_ATTR_READONLY 0x01
#define FS_ATTR_HIDDEN   0x02
#define FS_ATTR_SYSTEM   0x04
#define FS_ATTR_VOLUME   0x08
#define FS_ATTR_DIR      0x10
#define FS_ATTR_ARCHIVE  0x20

// 錯誤碼一律是負數，0 = 成功
#define FS_OK         0
#define FS_ENOENT    -1    // 找不到
#define FS_EEXIST    -2    // 已經存在
#define FS_ENOTDIR   -3    // 路徑中間有東西不是資料夾
#define FS_EISDIR    -4    // 是資料夾（但需要檔案）
#define FS_ENOSPC    -5    // 磁碟或目錄滿了
#define FS_EIO       -6    // 讀寫硬碟失敗
#define FS_EINVAL    -7    // 檔名不合法
#define FS_ENOTEMPTY -8    // 資料夾不是空的
#define FS_ENOFS     -9    // 沒有掛載檔案系統

struct fs_info {
    char     name[13];     // "HELLO.EXE"
    uint8_t  attr;
    uint32_t size;
    int      year, month, day, hour, minute;
};

int  fs_mount(void);
int  fs_stat(const char *path, struct fs_info *out);
int  fs_readdir(const char *dir, int *cookie, struct fs_info *out);  // 讀完回傳 FS_ENOENT

// 整個檔案讀進 kmalloc 的緩衝區（後面多補一個 \0），用完要 kfree
int  fs_read_file(const char *path, void **data, uint32_t *size);
// 建立或覆蓋整個檔案
int  fs_write_file(const char *path, const void *data, uint32_t size);

int  fs_delete(const char *path);
int  fs_mkdir(const char *path);
int  fs_rmdir(const char *path);
int  fs_rename(const char *path, const char *new_name);   // 只換名字，不搬資料夾

uint64_t    fs_free_bytes(void);
uint64_t    fs_total_bytes(void);
const char *fs_label(void);
const char *fs_strerror(int err);
