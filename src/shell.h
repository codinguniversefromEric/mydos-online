#pragma once
#include "fs.h"

__attribute__((noreturn)) void shell_run(void);

// 執行一行指令（批次檔也走這裡）
void shell_execute(const char *line);

// ---- 給各指令模組共用的工具 ----
extern char shell_cwd[FS_MAX_PATH];        // 目前目錄，例如 "\DOCS"；根目錄是 "\"

// 把使用者輸入（相對路徑、C:、.、..）換算成完整絕對路徑。0 = 成功
int  path_resolve(const char *in, char *out);
// 把 dir 和 name 接成一條路徑（處理根目錄的反斜線）
void path_join(char *out, const char *dir, const char *name);
// * 和 ? 萬用字元比對（不分大小寫）
int  wildcard_match(const char *pat, const char *name);
int  has_wildcard(const char *s);
// 1234567 → "1,234,567"
void fmt_commas(char *out, unsigned long long v);
// 切出下一個參數（空白分隔），回傳剩下的字串
char *next_arg(char *s, char **arg);

// ---- 指令表 ----
struct command {
    const char *name;
    const char *help;
    void (*fn)(char *args);
};

// cmd_fs.c 提供的檔案指令
void cmd_dir(char *args);
void cmd_cd(char *args);
void cmd_md(char *args);
void cmd_rd(char *args);
void cmd_type(char *args);
void cmd_copy(char *args);
void cmd_del(char *args);
void cmd_ren(char *args);
void cmd_vol(char *args);

// batch.c：執行批次檔（M8）
void batch_run(const char *path, const char *args);
int  batch_echo_enabled(void);
void batch_set_echo(int on);
void batch_goto(const char *label);
void batch_ctrl_c(void);
extern int shell_errorlevel;
