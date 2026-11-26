// 檔案相關指令：DIR CD MD RD TYPE COPY DEL REN VOL
#include "shell.h"
#include "fs.h"
#include "kprintf.h"
#include "keyboard.h"
#include "term.h"
#include "mm.h"
#include "string.h"

static void fs_error(int err) { kprintf("%s\n", fs_strerror(err)); }

// 把「C:\DOCS\*.TXT」拆成資料夾「\DOCS」和樣式「*.TXT」。
// 沒有萬用字元時：是資料夾就列整個資料夾，否則當成單一檔名。
static int split_pattern(const char *arg, char *dir, char *pat) {
    char full[FS_MAX_PATH];
    if (path_resolve(*arg ? arg : ".", full)) return FS_EINVAL;
    struct fs_info info;
    if (!has_wildcard(full) && !fs_stat(full, &info) && (info.attr & FS_ATTR_DIR)) {
        strcpy(dir, full);
        strcpy(pat, "*.*");
        return 0;
    }
    char *slash = strrchr(full, '\\');
    strncpy0(pat, slash + 1, 13);
    if (slash == full) strcpy(dir, "\\");
    else { *slash = 0; strcpy(dir, full); }
    return 0;
}

// ---------------------------------------------------------------------
void cmd_dir(char *args) {
    char dir[FS_MAX_PATH], pat[13], num[32];
    if (split_pattern(args, dir, pat)) { fs_error(FS_EINVAL); return; }

    kprintf("\n Volume in drive C is %s\n", fs_label());
    kprintf(" Directory of C:%s\n\n", dir);

    int cookie = 0, files = 0, dirs = 0, r;
    uint64_t bytes = 0;
    struct fs_info f;
    while ((r = fs_readdir(dir, &cookie, &f)) == 0) {
        if (f.attr & FS_ATTR_HIDDEN) continue;
        if (!wildcard_match(pat, f.name)) continue;
        // DOS 風格：名字和副檔名分兩欄
        char base[9] = "", ext[4] = "";
        char *dot = strchr(f.name, '.');
        if (dot && f.name[0] != '.') {
            strncpy0(base, f.name, dot - f.name + 1);
            strncpy0(ext, dot + 1, sizeof(ext));
        } else {
            strncpy0(base, f.name, sizeof(base));
        }
        if (f.attr & FS_ATTR_DIR) {
            kprintf("%-8s %-3s    <DIR>      ", base, ext);
            dirs++;
        } else {
            fmt_commas(num, f.size);
            kprintf("%-8s %-3s %13s  ", base, ext, num);
            files++;
            bytes += f.size;
        }
        if (f.year > 1980) kprintf("%04d-%02d-%02d  %02d:%02d", f.year, f.month, f.day, f.hour, f.minute);
        kprintf("\n");
    }
    if (r != FS_ENOENT) { fs_error(r); return; }
    if (!files && !dirs) { kprintf("File not found\n"); return; }
    fmt_commas(num, bytes);
    kprintf("%10d file(s) %14s bytes\n", files, num);
    fmt_commas(num, fs_free_bytes());
    kprintf("%10d dir(s)  %14s bytes free\n\n", dirs, num);
}

// ---------------------------------------------------------------------
void cmd_cd(char *args) {
    char *arg;
    next_arg(args, &arg);
    if (!*arg) { kprintf("C:%s\n", shell_cwd); return; }   // 沒參數：顯示目前目錄
    char full[FS_MAX_PATH];
    struct fs_info info;
    if (path_resolve(arg, full) || fs_stat(full, &info) || !(info.attr & FS_ATTR_DIR)) {
        kprintf("Invalid directory\n");
        return;
    }
    strcpy(shell_cwd, full);
}

void cmd_md(char *args) {
    char *arg, full[FS_MAX_PATH];
    next_arg(args, &arg);
    if (!*arg) { kprintf("Required parameter missing\n"); return; }
    int r = path_resolve(arg, full) ? FS_EINVAL : fs_mkdir(full);
    if (r == FS_EEXIST) kprintf("Directory already exists\n");
    else if (r) fs_error(r);
}

void cmd_rd(char *args) {
    char *arg, full[FS_MAX_PATH];
    next_arg(args, &arg);
    if (!*arg) { kprintf("Required parameter missing\n"); return; }
    if (path_resolve(arg, full)) { fs_error(FS_EINVAL); return; }
    if (!strncmp(shell_cwd, full, strlen(full)) &&
        (shell_cwd[strlen(full)] == 0 || shell_cwd[strlen(full)] == '\\')) {
        kprintf("Attempt to remove current directory\n");
        return;
    }
    int r = fs_rmdir(full);
    if (r == FS_ENOTDIR || r == FS_ENOENT) kprintf("Invalid path, not directory,\nor directory not empty\n");
    else if (r) fs_error(r);
}

// ---------------------------------------------------------------------
void cmd_type(char *args) {
    char *arg, full[FS_MAX_PATH];
    next_arg(args, &arg);
    if (!*arg) { kprintf("Required parameter missing\n"); return; }
    if (path_resolve(arg, full)) { fs_error(FS_EINVAL); return; }
    void *data;
    uint32_t size;
    int r = fs_read_file(full, &data, &size);
    if (r) { fs_error(r); return; }
    const char *p = data;
    for (uint32_t i = 0; i < size; i++) {
        if (p[i] == 26) break;                     // Ctrl+Z：DOS 文字檔的結尾記號
        if (p[i] == '\r') continue;                // \r\n 只要換一次行
        term_putc(p[i]);
    }
    if (size && p[size - 1] != '\n') term_putc('\n');
    kfree(data);
}

// ---------------------------------------------------------------------
// COPY CON 檔名：從鍵盤打字存成檔案，按 Ctrl+Z 再按 Enter 結束（DOS 經典用法）
static void copy_con(const char *dst) {
    uint32_t cap = 4096, len = 0, line_start = 0;
    char *buf = kmalloc(cap);
    for (;;) {
        int c = keyboard_getc();
        if (c == 3) { kprintf("^C\n"); kfree(buf); return; }    // Ctrl+C：放棄
        if (c == '\n') {
            term_putc('\n');
            if (len && buf[len - 1] == 26) { len--; break; }    // 「^Z」後按 Enter：結束
        } else if (c == '\b') {
            if (len > line_start) {
                len--;
                term_putc('\b');
                if (buf[len] == 26) term_putc('\b');            // ^Z 佔兩格
            }
            continue;
        } else if (c == 26) {
            kprintf("^Z");
        } else if (c >= ' ' && c <= '~') {
            term_putc((char)c);
        } else {
            continue;                                           // 其他控制字元忽略
        }
        if (len + 1 > cap) {                                    // 滿了就換兩倍大的
            char *bigger = kmalloc(cap * 2);
            memcpy(bigger, buf, len);
            kfree(buf);
            buf = bigger;
            cap *= 2;
        }
        buf[len++] = (char)c;
        if (c == '\n') line_start = len;
    }
    int r = fs_write_file(dst, buf, len);
    kfree(buf);
    if (r) fs_error(r);
    else kprintf("        1 file(s) copied\n");
}

static int copy_one(const char *src, const char *dst) {
    void *data;
    uint32_t size;
    int r = fs_read_file(src, &data, &size);
    if (r) return r;
    r = fs_write_file(dst, data, size);
    kfree(data);
    return r;
}

void cmd_copy(char *args) {
    char *a, *b;
    args = next_arg(args, &a);
    next_arg(args, &b);
    if (!*a) { kprintf("Required parameter missing\n"); return; }

    char dst[FS_MAX_PATH];
    if (path_resolve(*b ? b : ".", dst)) { fs_error(FS_EINVAL); return; }
    struct fs_info info;
    int dst_is_dir = !fs_stat(dst, &info) && (info.attr & FS_ATTR_DIR);

    if (!stricmp(a, "CON")) {
        if (!*b || dst_is_dir) { kprintf("Usage: COPY CON filename\n"); return; }
        copy_con(dst);
        return;
    }

    char dir[FS_MAX_PATH], pat[13], src[FS_MAX_PATH], target[FS_MAX_PATH];
    if (split_pattern(a, dir, pat)) { fs_error(FS_EINVAL); return; }
    int cookie = 0, count = 0, multi = has_wildcard(pat);
    struct fs_info f;
    if (multi && !dst_is_dir) { kprintf("Target must be a directory\n"); return; }
    while (fs_readdir(dir, &cookie, &f) == 0) {
        if ((f.attr & FS_ATTR_DIR) || !wildcard_match(pat, f.name)) continue;
        path_join(src, dir, f.name);
        if (dst_is_dir) path_join(target, dst, f.name);
        else strcpy(target, dst);
        if (!stricmp(src, target)) { kprintf("File cannot be copied onto itself\n"); return; }
        if (multi) kprintf("%s\n", f.name);
        int r = copy_one(src, target);
        if (r) { fs_error(r); break; }
        count++;
    }
    if (!count) kprintf("File not found\n");
    kprintf("%9d file(s) copied\n", count);
}

// ---------------------------------------------------------------------
void cmd_del(char *args) {
    char *arg;
    next_arg(args, &arg);
    if (!*arg) { kprintf("Required parameter missing\n"); return; }
    char dir[FS_MAX_PATH], pat[13], path[FS_MAX_PATH];
    if (split_pattern(arg, dir, pat)) { fs_error(FS_EINVAL); return; }

    if (!strcmp(pat, "*.*")) {                     // 跟 DOS 一樣，刪全部要確認
        kprintf("All files in directory will be deleted!\nAre you sure (Y/N)?");
        int c = keyboard_getc();
        kprintf("%c\n", (c >= ' ' && c <= '~') ? c : ' ');
        if (c != 'y' && c != 'Y') return;
    }
    // 先數一遍再刪：邊讀目錄邊刪除，位置會跑掉
    int cookie = 0, n = 0;
    struct fs_info f;
    char (*names)[13] = kmalloc(512 * 13);
    while (n < 512 && fs_readdir(dir, &cookie, &f) == 0)
        if (!(f.attr & FS_ATTR_DIR) && wildcard_match(pat, f.name)) strcpy(names[n++], f.name);
    if (!n) kprintf("File not found\n");
    for (int i = 0; i < n; i++) {
        path_join(path, dir, names[i]);
        int r = fs_delete(path);
        if (r) { kprintf("%s: ", names[i]); fs_error(r); }
    }
    kfree(names);
}

void cmd_ren(char *args) {
    char *a, *b, full[FS_MAX_PATH];
    args = next_arg(args, &a);
    next_arg(args, &b);
    if (!*a || !*b) { kprintf("Usage: REN oldname newname\n"); return; }
    if (strchr(b, '\\')) { kprintf("New name cannot contain a path\n"); return; }
    if (path_resolve(a, full)) { fs_error(FS_EINVAL); return; }
    int r = fs_rename(full, b);
    if (r == FS_EEXIST) kprintf("Duplicate file name or file not found\n");
    else if (r) fs_error(r);
}

void cmd_vol(char *args) {
    (void)args;
    char a[32], b[32];
    fmt_commas(a, fs_total_bytes());
    fmt_commas(b, fs_free_bytes());
    kprintf("\n Volume in drive C is %s\n", fs_label());
    kprintf(" %s bytes total disk space\n %s bytes available on disk\n\n", a, b);
}
