#include "mydos.h"

// =====================================================================
//  系統呼叫：把編號放 rax、參數放 rdi rsi rdx，執行 int 0x80
// =====================================================================
static inline int64_t syscall3(int64_t n, int64_t a, int64_t b, int64_t c) {
    int64_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return ret;
}
#define SC0(n)       syscall3(n, 0, 0, 0)
#define SC1(n, a)    syscall3(n, (int64_t)(a), 0, 0)
#define SC2(n, a, b) syscall3(n, (int64_t)(a), (int64_t)(b), 0)

void exit(int code)                      { SC1(SYS_EXIT, code); for (;;) {} }
int  getkey(void)                        { return SC0(SYS_GETKEY); }
int  pollkey(void)                       { return SC0(SYS_POLLKEY); }
int  readline(char *b, int max)          { return SC2(SYS_READLINE, b, max); }
int  open(const char *p, int mode)       { return SC2(SYS_OPEN, p, mode); }
int  read(int fd, void *b, int n)        { return syscall3(SYS_READ, fd, (int64_t)b, n); }
int  write(int fd, const void *b, int n) { return syscall3(SYS_WRITE, fd, (int64_t)b, n); }
int  close(int fd)                       { return SC1(SYS_CLOSE, fd); }
int  delete(const char *p)               { return SC1(SYS_DELETE, p); }
uint64_t ticks(void)                     { return SC0(SYS_TICKS); }
void sleep(int ms)                       { SC1(SYS_SLEEP, ms); }
void gettime(struct datetime *t)         { SC1(SYS_TIME, t); }
void cls(void)                           { SC0(SYS_CLS); }
void color(uint32_t fg, uint32_t bg)     { SC2(SYS_COLOR, fg, bg); }
int  screen_cols(void)                   { return (uint64_t)SC0(SYS_SCREEN) >> 32; }
int  screen_rows(void)                   { return SC0(SYS_SCREEN) & 0xFFFFFFFF; }
void gotoxy(int col, int row)            { SC2(SYS_GOTO, col, row); }

// =====================================================================
//  程式進入點：核心把 argc 放 rdi、argv 放 rsi，跟一般函式呼叫一樣
// =====================================================================
int main(int argc, char **argv);

__attribute__((section(".text._start")))
void _start(int argc, char **argv) {
    exit(main(argc, argv));
}

// =====================================================================
//  字串
// =====================================================================
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
void *memcpy(void *d, const void *s, size_t n) { char *x = d; const char *y = s; while (n--) *x++ = *y++; return d; }
void *memset(void *d, int c, size_t n) { char *x = d; while (n--) *x++ = (char)c; return d; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int atoi(const char *s) {
    int v = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

// =====================================================================
//  printf：跟核心的 kprintf 同一套做法，只是輸出先存進緩衝區再一次送出
// =====================================================================
struct out { char *buf; size_t cap, len; };
static void emit(struct out *o, char c) { if (o->len + 1 < o->cap) o->buf[o->len] = c; o->len++; }

static void fmt_num(struct out *o, uint64_t u, int neg, int base, int width, char pad, int left) {
    char tmp[24]; int n = 0;
    do { tmp[n++] = "0123456789abcdef"[u % base]; u /= base; } while (u);
    int total = n + neg;
    if (!left && pad == ' ') while (total < width) { emit(o, ' '); width--; }
    if (neg) emit(o, '-');
    if (!left && pad == '0') while (total < width) { emit(o, '0'); width--; }
    while (n) emit(o, tmp[--n]);
    if (left) while (total < width) { emit(o, ' '); width--; }
}

static void vformat(struct out *o, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { emit(o, *f); continue; }
        f++;
        char pad = ' '; int left = 0, width = 0;
        for (;; f++) { if (*f == '0') pad = '0'; else if (*f == '-') left = 1; else break; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') f++;
        switch (*f) {
        case 'd': { long v = va_arg(ap, int); fmt_num(o, v < 0 ? -(uint64_t)v : (uint64_t)v, v < 0, 10, width, pad, left); break; }
        case 'u': fmt_num(o, va_arg(ap, unsigned), 0, 10, width, pad, left); break;
        case 'x': fmt_num(o, va_arg(ap, unsigned), 0, 16, width, pad, left); break;
        case 'c': emit(o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
            int n = strlen(s);
            if (!left) while (n < width--) emit(o, ' ');
            while (*s) emit(o, *s++);
            if (left) while (n < width--) emit(o, ' ');
            break;
        }
        case '%': emit(o, '%'); break;
        case 0: return;
        }
    }
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    struct out o = { buf, n, 0 };
    va_list ap; va_start(ap, fmt); vformat(&o, fmt, ap); va_end(ap);
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return o.len;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    struct out o = { buf, sizeof(buf), 0 };
    va_list ap; va_start(ap, fmt); vformat(&o, fmt, ap); va_end(ap);
    size_t n = o.len < sizeof(buf) ? o.len : sizeof(buf) - 1;
    SC2(SYS_PUTS, buf, n);
    return n;
}

void print(const char *s) { SC2(SYS_PUTS, s, strlen(s)); }
void putchar(char c) { SC2(SYS_PUTS, &c, 1); }

// =====================================================================
//  malloc：最簡單的「只往前切」配置器。free 什麼都不做——
//  程式結束時核心會把整個程式的記憶體收回，對小程式來說夠用了。
// =====================================================================
static uint8_t arena[1024 * 1024];
static size_t used;
void *malloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (used + n > sizeof(arena)) return 0;
    void *p = arena + used;
    used += n;
    return p;
}
void free(void *p) { (void)p; }

// ---- 亂數：線性同餘法 ----
static uint32_t seed = 1;
void srand(uint32_t s) { seed = s ? s : 1; }
uint32_t rand(void) { seed = seed * 1103515245 + 12345; return (seed >> 16) & 0x7FFF; }
