#include "shell.h"
#include "keyboard.h"
#include "kprintf.h"
#include "term.h"
#include "panic.h"
#include "io.h"
#include "mm.h"
#include "string.h"
#include "timer.h"
#include "version.h"
#include "process.h"

#define LINE_MAX 120

char shell_cwd[FS_MAX_PATH] = "\\";

// =====================================================================
//  路徑工具
// =====================================================================
void path_join(char *out, const char *dir, const char *name) {
    strncpy0(out, dir, FS_MAX_PATH);
    size_t n = strlen(out);
    if (n == 0 || out[n - 1] != '\\') { if (n + 1 < FS_MAX_PATH) { out[n++] = '\\'; out[n] = 0; } }
    if (n + strlen(name) < FS_MAX_PATH) strcat(out, name);
}

int path_resolve(const char *in, char *out) {
    char tmp[FS_MAX_PATH * 2];
    // 只有一顆 C 槽，「C:」直接跳過
    if ((in[0] == 'c' || in[0] == 'C') && in[1] == ':') in += 2;
    if (in[0] == '\\' || in[0] == '/') {           // 絕對路徑
        strncpy0(tmp, in, sizeof(tmp));
    } else {                                       // 相對路徑：接在目前目錄後面
        strncpy0(tmp, shell_cwd, sizeof(tmp));
        if (strlen(tmp) + 1 + strlen(in) >= sizeof(tmp)) return -1;
        strcat(tmp, "\\");
        strcat(tmp, in);
    }

    // 一段一段處理：. 不動、.. 退回上一層、其他的接上去
    char result[FS_MAX_PATH] = "";
    size_t rlen = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '\\' && *p != '/') p++;
        size_t n = p - start;
        if (n == 1 && start[0] == '.') continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            while (rlen > 0 && result[rlen - 1] != '\\') rlen--;
            if (rlen > 0) rlen--;                  // 吃掉那個反斜線
            result[rlen] = 0;
            continue;
        }
        if (rlen + 1 + n >= FS_MAX_PATH) return -1;
        result[rlen++] = '\\';
        for (size_t i = 0; i < n; i++) result[rlen++] = toupper(start[i]);
        result[rlen] = 0;
    }
    if (rlen == 0) strcpy(out, "\\");
    else strcpy(out, result);
    return 0;
}

int has_wildcard(const char *s) { return strchr(s, '*') || strchr(s, '?'); }

// * 可以吃掉任意多個字元（包括 0 個），? 剛好吃一個。
// 為了 DOS 習慣，「*.*」和「*」都比對所有檔案（包括沒有副檔名的）。
int wildcard_match(const char *pat, const char *name) {
    if (!strcmp(pat, "*.*")) return 1;
    if (*pat == 0) return *name == 0;
    if (*pat == '*') {
        for (const char *n = name;; n++) {
            if (wildcard_match(pat + 1, n)) return 1;
            if (!*n) return 0;
        }
    }
    if (*name == 0) return !strcmp(pat, ".*") || !strcmp(pat, ".");   // 「README.*」比對 README
    if (*pat == '?' || toupper(*pat) == toupper(*name))
        return wildcard_match(pat + 1, name + 1);
    return 0;
}

void fmt_commas(char *out, unsigned long long v) {
    char tmp[32];
    int n = 0, digits = 0;
    do {
        if (digits && digits % 3 == 0) tmp[n++] = ',';
        tmp[n++] = '0' + v % 10;
        v /= 10;
        digits++;
    } while (v);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

char *next_arg(char *s, char **arg) {
    while (*s == ' ') s++;
    *arg = s;
    while (*s && *s != ' ') s++;
    if (*s) *s++ = 0;
    while (*s == ' ') s++;
    return s;
}

// =====================================================================
//  讀一行
// =====================================================================
#define HISTORY 16
static char history[HISTORY][LINE_MAX + 1];
static int  hist_count;                    // 總共存過幾筆（可能超過 HISTORY）

static void replace_line(char *line, int *len, const char *with) {
    while (*len > 0) { (*len)--; term_putc('\b'); }   // 擦掉目前這行
    strncpy0(line, with, LINE_MAX + 1);
    *len = strlen(line);
    kprintf("%s", line);
}

static int read_line(char *line, int max) {
    int len = 0;
    int browse = hist_count;               // 上下鍵目前看到第幾筆；= hist_count 代表「新的一行」
    for (;;) {
        int c = keyboard_getc();
        if (c == '\n') {
            term_putc('\n');
            line[len] = 0;
            // 存進歷史（空行和跟上一筆一樣的不存）
            if (len && (!hist_count || strcmp(history[(hist_count - 1) % HISTORY], line)))
                strcpy(history[hist_count++ % HISTORY], line);
            return len;
        } else if (c == '\b') {
            if (len > 0) { len--; term_putc('\b'); }
        } else if (c == 3) {                       // Ctrl+C
            kprintf("^C\n");
            line[0] = 0;
            return 0;
        } else if (c == 27) {                      // Esc：整行清掉
            replace_line(line, &len, "");
        } else if (c == KEY_UP) {
            int oldest = hist_count > HISTORY ? hist_count - HISTORY : 0;
            if (browse > oldest) { browse--; replace_line(line, &len, history[browse % HISTORY]); }
        } else if (c == KEY_DOWN) {
            if (browse < hist_count) browse++;
            replace_line(line, &len, browse < hist_count ? history[browse % HISTORY] : "");
        } else if (c == KEY_F3) {                  // DOS 的 F3：叫回上一個指令
            if (hist_count) replace_line(line, &len, history[(hist_count - 1) % HISTORY]);
        } else if (c >= ' ' && c <= '~' && len < max) {
            line[len++] = (char)c;
            term_putc((char)c);
        }
    }
}

// =====================================================================
//  一般指令
// =====================================================================
static void cmd_help(char *args);
void shell_execute_program(const char *name, const char *args);
static void cmd_ver(char *args)  { (void)args; kprintf("\nMYDOS Version %s\n\n", MYDOS_VERSION); }
static void cmd_cls(char *args)  { (void)args; term_clear(); }
static void cmd_echo(char *args) {
    if (!*args)                    kprintf("ECHO is %s\n", batch_echo_enabled() ? "on" : "off");
    else if (!stricmp(args, "OFF")) batch_set_echo(0);
    else if (!stricmp(args, "ON"))  batch_set_echo(1);
    else                           kprintf("%s\n", args);
}

static void cmd_rem(char *args)  { (void)args; }
static void cmd_goto(char *args) { char *l; next_arg(args, &l); batch_goto(l); }

static void cmd_pause(char *args) {
    (void)args;
    kprintf("Press any key to continue . . .");
    int c = keyboard_getc();
    kprintf("%s\n", c == 3 ? "^C" : "");
    if (c == 3) batch_ctrl_c();             // 跟 DOS 一樣：問要不要中止批次檔
}

// MYDOS 的批次檔執行完一律回到呼叫者，所以 CALL 跟直接打名字一樣；
// 保留這個指令是為了相容 DOS 的寫法
static void cmd_call(char *args) {
    char *name;
    args = next_arg(args, &name);
    if (*name) shell_execute_program(name, args);
}

// IF [NOT] EXIST 檔案 指令 / IF [NOT] ERRORLEVEL n 指令 / IF [NOT] a==b 指令
static void cmd_if(char *args) {
    char *word, full[FS_MAX_PATH];
    int negate = 0, cond;
    char *rest = next_arg(args, &word);
    if (!stricmp(word, "NOT")) { negate = 1; rest = next_arg(rest, &word); }

    if (!stricmp(word, "EXIST")) {
        char *file;
        rest = next_arg(rest, &file);
        struct fs_info info;
        cond = !path_resolve(file, full) && !fs_stat(full, &info);
    } else if (!stricmp(word, "ERRORLEVEL")) {
        char *n;
        rest = next_arg(rest, &n);
        cond = shell_errorlevel >= atol(n);
    } else {
        char *eq = strchr(word, '=');               // word 是「a==b」
        if (!eq || eq[1] != '=') { kprintf("Syntax error\n"); return; }
        *eq = 0;
        cond = !strcmp(word, eq + 2);
    }
    if (cond != negate) shell_execute(rest);
}

static void cmd_time(char *args) {
    (void)args;
    struct datetime t;
    rtc_read(&t);
    kprintf("Current time is %02d:%02d:%02d\n", t.hour, t.minute, t.second);
}

static void cmd_date(char *args) {
    (void)args;
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };   // Sakamoto 星期公式
    struct datetime t;
    rtc_read(&t);
    int y = t.year - (t.month < 3);
    int w = (y + y / 4 - y / 100 + y / 400 + k[t.month - 1] + t.day) % 7;
    kprintf("Current date is %s %04d-%02d-%02d\n", wd[w], t.year, t.month, t.day);
}

static void cmd_uptime(char *args) {
    (void)args;
    uint64_t s = timer_ticks() / TIMER_HZ;
    kprintf("Up %llu:%02llu:%02llu\n", s / 3600, (s / 60) % 60, s % 60);
}

static void cmd_mem(char *args) {
    (void)args;
    char a[32], b[32], c[32];
    uint64_t total = pmm_total_pages() * PAGE_SIZE;
    uint64_t freeb = pmm_free_pages() * PAGE_SIZE;
    size_t used, hfree, mapped;
    heap_stats(&used, &hfree, &mapped);
    fmt_commas(a, total); fmt_commas(b, total - freeb); fmt_commas(c, freeb);
    kprintf("\n  Memory Type        Total          Used          Free\n");
    kprintf("  -----------  ------------  ------------  ------------\n");
    kprintf("  Physical     %12s  %12s  %12s\n", a, b, c);
    fmt_commas(a, mapped); fmt_commas(b, used); fmt_commas(c, hfree);
    kprintf("  Kernel heap  %12s  %12s  %12s\n\n", a, b, c);
}

// COLOR 1F = 藍底（1）白字（F），跟 DOS 一樣用 16 色編號
static void cmd_color(char *args) {
    static const uint32_t pal[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xC0C0C0,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };
    int v[2];
    if (strlen(args) != 2) { kprintf("Usage: COLOR bf   (b = background, f = foreground, 0-F)\n"); return; }
    for (int i = 0; i < 2; i++) {
        char ch = toupper(args[i]);
        if (ch >= '0' && ch <= '9') v[i] = ch - '0';
        else if (ch >= 'A' && ch <= 'F') v[i] = ch - 'A' + 10;
        else { kprintf("Invalid color\n"); return; }
    }
    if (v[0] == v[1]) { kprintf("Background and foreground must differ\n"); return; }
    term_set_color(pal[v[1]], pal[v[0]]);
    term_clear();
}

static void cmd_crash(char *args) {
    (void)args;
    volatile int one = 1, zero = 0;        // 兩個都要 volatile，否則 GCC 不產生除法指令
    kprintf("%d\n", one / zero);
}

static void cmd_reboot(char *args) {
    (void)args;
    kprintf("Rebooting...\n");
    while (inb(0x64) & 2) {}
    outb(0x64, 0xFE);                      // 鍵盤控制器拉 CPU 的 reset 腳
    panic("Reboot failed");
}

static const struct command commands[] = {
    { "HELP",   "List commands",                          cmd_help   },
    { "DIR",    "List files: DIR [path] [*.TXT]",         cmd_dir    },
    { "CD",     "Change directory: CD path, CD ..",       cmd_cd     },
    { "MD",     "Make directory",                         cmd_md     },
    { "RD",     "Remove empty directory",                 cmd_rd     },
    { "TYPE",   "Show a text file",                       cmd_type   },
    { "COPY",   "Copy files; COPY CON file to type one",  cmd_copy   },
    { "DEL",    "Delete files (wildcards OK)",            cmd_del    },
    { "REN",    "Rename: REN old new",                    cmd_ren    },
    { "VOL",    "Show disk label and space",              cmd_vol    },
    { "ECHO",   "Print text; ECHO OFF in batch files",    cmd_echo   },
    { "REM",    "Comment (batch files)",                  cmd_rem    },
    { "PAUSE",  "Wait for a key",                         cmd_pause  },
    { "GOTO",   "Jump to :label (batch files)",           cmd_goto   },
    { "IF",     "IF [NOT] EXIST/ERRORLEVEL/a==b command", cmd_if     },
    { "CALL",   "Run a batch file and come back",         cmd_call   },
    { "CLS",    "Clear the screen",                       cmd_cls    },
    { "COLOR",  "Set colors: COLOR 1F",                   cmd_color  },
    { "TIME",   "Show the time",                          cmd_time   },
    { "DATE",   "Show the date",                          cmd_date   },
    { "UPTIME", "Time since boot",                        cmd_uptime },
    { "MEM",    "Show memory usage",                      cmd_mem    },
    { "VER",    "Show MYDOS version",                     cmd_ver    },
    { "PANIC",  "Crash the kernel (test blue screen)",    cmd_crash  },
    { "REBOOT", "Restart the computer",                   cmd_reboot },
    // 別名：DOS 的長名字也能用
    { "CHDIR",  0, cmd_cd  }, { "MKDIR", 0, cmd_md  }, { "RMDIR",  0, cmd_rd  },
    { "ERASE",  0, cmd_del }, { "RENAME", 0, cmd_ren },
};
#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(char *args) {
    (void)args;
    for (unsigned i = 0; i < NCOMMANDS; i++)
        if (commands[i].help) kprintf("  %-8s%s\n", commands[i].name, commands[i].help);
}

// =====================================================================
//  執行
// =====================================================================
// ---------------------------------------------------------------------
//  找程式：先找目前目錄，再找 \BIN（就像 DOS 的 PATH）
//  沒寫副檔名時依序試 .EXE、.BAT
// ---------------------------------------------------------------------
int shell_errorlevel;

static int find_program(const char *name, char *out) {
    if (!*name) return -1;
    static const char *exts[] = { ".EXE", ".BAT" };
    const char *dirs[2] = { shell_cwd, "\\BIN" };
    int has_ext = strchr(name, '.') != 0;
    int has_path = strchr(name, '\\') != 0 || name[1] == ':';
    struct fs_info info;
    char cand[FS_MAX_PATH], tmp[FS_MAX_PATH];
    for (int d = 0; d < (has_path ? 1 : 2); d++) {
        for (int e = 0; e < (has_ext ? 1 : 2); e++) {
            if (has_path) strncpy0(tmp, name, sizeof(tmp));
            else path_join(tmp, dirs[d], name);
            if (!has_ext) strcat(tmp, exts[e]);
            if (path_resolve(tmp, cand)) return -1;
            if (!fs_stat(cand, &info) && !(info.attr & FS_ATTR_DIR)) {
                strcpy(out, cand);
                return 0;
            }
        }
    }
    return -1;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !stricmp(s + a - b, suffix);
}

static void run_program(const char *path, const char *args) {
    if (ends_with(path, ".EXE")) {
        uint32_t fg, bg;
        term_get_color(&fg, &bg);
        int code = process_run(path, args);
        if (code < 0) {                            // 當掉或被 Ctrl+C：還原顏色、丟掉殘留按鍵
            term_set_color(fg, bg);
            keyboard_flush();
        }
        shell_errorlevel = code;
    } else if (ends_with(path, ".BAT")) {
        batch_run(path, args);
    } else {
        kprintf("Cannot run %s\n", path);
    }
}

void shell_execute(const char *input) {
    char line[LINE_MAX + 1];
    strncpy0(line, input, sizeof(line));
    char *name, *args = next_arg(line, &name);
    if (!*name) return;

    // 「CD..」「CD\」這種沒空白的寫法，DOS 也接受
    if (!strncmp(name, "ECHO.", 5) || !strncmp(name, "echo.", 5)) {   // ECHO. = 空白行
        kprintf("%s%s%s\n", name + 5, *args ? " " : "", args);
        return;
    }
    if (toupper(name[0]) == 'C' && toupper(name[1]) == 'D' && (name[2] == '.' || name[2] == '\\')) {
        cmd_cd(name + 2);
        return;
    }

    for (unsigned i = 0; i < NCOMMANDS; i++) {
        if (!stricmp(name, commands[i].name)) {
            commands[i].fn(args);
            return;
        }
    }
    shell_execute_program(name, args);
}

void shell_execute_program(const char *name, const char *args) {
    char path[FS_MAX_PATH];
    if (!find_program(name, path)) run_program(path, args);
    else kprintf("Bad command or file name\n");
}

void shell_run(void) {
    char line[LINE_MAX + 1];
    struct fs_info info;
    if (!fs_stat("\\AUTOEXEC.BAT", &info)) batch_run("\\AUTOEXEC.BAT", "");
    else kprintf("Type HELP for a list of commands.\n\n");
    for (;;) {
        kprintf("C:%s>", shell_cwd);
        read_line(line, LINE_MAX);
        shell_execute(line);
    }
}
