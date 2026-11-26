// 批次檔（.BAT）：把一串指令存在文字檔裡，一行一行交給命令列執行。
//
// 支援：
//   @指令            這一行不要顯示
//   ECHO OFF / ON    關掉／打開「執行前先顯示指令」
//   REM 註解
//   PAUSE            等使用者按鍵
//   %0 … %9          參數（%0 是批次檔自己的名字）
//   :標籤 / GOTO 標籤
//   IF [NOT] EXIST 檔案 指令
//   IF [NOT] ERRORLEVEL n 指令     （上一支程式的結束碼 >= n）
//   IF [NOT] 字串1==字串2 指令
//   CALL 別的.BAT    執行完再回來
#include "shell.h"
#include "kprintf.h"
#include "keyboard.h"
#include "mm.h"
#include "string.h"

#define MAX_DEPTH 4

struct batch {
    char *text;              // 整個檔案
    char *args[10];          // %0 … %9
    char  argbuf[128];
    char  goto_label[16];    // GOTO 設定，主迴圈負責跳
    int   echo;
    int   abort;             // 使用者選擇中止
};

static struct batch *stack[MAX_DEPTH];
static int depth;

int batch_echo_enabled(void) { return depth == 0 || stack[depth - 1]->echo; }

void batch_set_echo(int on) {
    if (depth) stack[depth - 1]->echo = on;
}

void batch_goto(const char *label) {
    if (!depth) { kprintf("GOTO only works in batch files\n"); return; }
    if (*label == ':') label++;
    strncpy0(stack[depth - 1]->goto_label, label, sizeof(stack[0]->goto_label));
}

// 把 %1 換成參數，%% 換成 %
static void expand(const struct batch *b, const char *in, char *out, size_t max) {
    size_t n = 0;
    while (*in && n + 1 < max) {
        if (in[0] == '%' && in[1] >= '0' && in[1] <= '9') {
            const char *a = b->args[in[1] - '0'];
            while (a && *a && n + 1 < max) out[n++] = *a++;
            in += 2;
        } else if (in[0] == '%' && in[1] == '%') {
            out[n++] = '%';
            in += 2;
        } else {
            out[n++] = *in++;
        }
    }
    out[n] = 0;
}

// 在檔案裡找「:label」那一行，回傳下一行的開頭
static char *find_label(char *text, const char *label) {
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char *p = line;
        while (*p == ' ') p++;
        if (*p == ':') {
            p++;
            size_t l = strlen(label), i = 0;
            while (i < l && toupper(p[i]) == toupper(label[i])) i++;   // 不分大小寫
            if (i == l && (p[i] == '\r' || p[i] == '\n' || p[i] == ' ' || p[i] == 0))
                return end ? end + 1 : line + len;
        }
        if (!end) break;
        line = end + 1;
    }
    return 0;
}

// 按了 Ctrl+C：問要不要中止，要的話所有層的批次檔都停下來
void batch_ctrl_c(void) {
    if (!depth) return;
    kprintf("Terminate batch job (Y/N)? ");
    int c = keyboard_getc();
    kprintf("%c\n", (c >= ' ' && c <= '~') ? c : ' ');
    if (c == 'y' || c == 'Y')
        for (int i = 0; i < depth; i++) stack[i]->abort = 1;
}

// 每一行開始前檢查：執行中按了 Ctrl+C 嗎？
static int user_wants_stop(struct batch *b) {
    if (keyboard_poll() == 3) {
        kprintf("^C\n");
        batch_ctrl_c();
    }
    return b->abort;
}

void batch_run(const char *path, const char *args) {
    if (depth >= MAX_DEPTH) { kprintf("Batch files nested too deeply\n"); return; }
    struct batch *b = kzalloc(sizeof(*b));
    uint32_t size;
    int r = fs_read_file(path, (void **)&b->text, &size);
    if (r) { kprintf("%s\n", fs_strerror(r)); kfree(b); return; }

    // 參數：%0 = 檔名，其餘照空白切開
    const char *name = strrchr(path, '\\');
    ksnprintf(b->argbuf, sizeof(b->argbuf), "%s %s", name ? name + 1 : path, args);
    char *p = b->argbuf;
    for (int i = 0; i < 10 && *p; i++) p = next_arg(p, &b->args[i]);
    b->echo = depth ? stack[depth - 1]->echo : 1;
    stack[depth++] = b;

    char *line = b->text;
    while (line && *line) {
        char *end = strchr(line, '\n');
        char raw[128], cmd[160];
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        memcpy(raw, line, len);
        raw[len] = 0;
        if (len && raw[len - 1] == '\r') raw[len - 1] = 0;   // DOS 換行是 \r\n
        line = end ? end + 1 : 0;

        if (user_wants_stop(b)) break;

        char *s = raw;
        while (*s == ' ') s++;
        if (!*s || *s == ':') continue;                       // 空行、標籤
        int quiet = 0;
        if (*s == '@') { quiet = 1; s++; }
        expand(b, s, cmd, sizeof(cmd));
        if (b->echo && !quiet) kprintf("C:%s>%s\n", shell_cwd, cmd);

        shell_execute(cmd);
        if (b->abort) break;                                  // PAUSE 時按了 Ctrl+C

        if (b->goto_label[0]) {                               // 剛剛執行了 GOTO
            line = find_label(b->text, b->goto_label);
            if (!line) { kprintf("Label not found: %s\n", b->goto_label); break; }
            b->goto_label[0] = 0;
        }
    }

    depth--;
    kfree(b->text);
    kfree(b);
}
