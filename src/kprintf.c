#include "kprintf.h"
#include "term.h"
#include <stdint.h>

// 格式化的邏輯只寫一次，「字元要送去哪」用 sink 抽換：
// kprintf → 終端機，ksnprintf → 字串緩衝區。
struct sink {
    void (*put)(struct sink *s, char c);
    char  *buf;
    size_t size, len;
};

static void put_term(struct sink *s, char c) { (void)s; term_putc(c); }
static void put_buf(struct sink *s, char c) {
    if (s->len + 1 < s->size) s->buf[s->len] = c;
    s->len++;
}

static void emit(struct sink *s, char c) { s->put(s, c); }
static void pad_n(struct sink *s, char c, int n) { while (n-- > 0) emit(s, c); }

static void put_num(struct sink *s, uint64_t u, int neg, unsigned base, int upper,
                    int width, char pad, int left) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int len = 0;
    do { tmp[len++] = digits[u % base]; u /= base; } while (u);
    int total = len + neg;
    if (left) {                          // 靠左：數字先印，空白補在後面
        if (neg) emit(s, '-');
        while (len) emit(s, tmp[--len]);
        pad_n(s, ' ', width - total);
        return;
    }
    if (neg && pad == '0') emit(s, '-');
    pad_n(s, pad, width - total);
    if (neg && pad == ' ') emit(s, '-');
    while (len) emit(s, tmp[--len]);
}

static void format(struct sink *s, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(s, *fmt); continue; }
        fmt++;
        char pad = ' ';
        int left = 0, width = 0, lng = 0;
        for (;; fmt++) {                                   // 1. 旗標
            if (*fmt == '0') pad = '0';
            else if (*fmt == '-') left = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')                 // 2. 寬度
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng++; fmt++; }              // 3. 長度
        switch (*fmt) {                                    // 4. 轉換字元
        case 'c': emit(s, (char)va_arg(ap, int)); break;
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            int n = 0;
            while (str[n]) n++;
            if (!left) pad_n(s, ' ', width - n);
            for (int i = 0; i < n; i++) emit(s, str[i]);
            if (left) pad_n(s, ' ', width - n);
            break;
        }
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, long long) : va_arg(ap, int);
            uint64_t mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            put_num(s, mag, v < 0, 10, 0, width, pad, left);
            break;
        }
        case 'u':
            put_num(s, lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned),
                    0, 10, 0, width, pad, left);
            break;
        case 'x': case 'X':
            put_num(s, lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned),
                    0, 16, *fmt == 'X', width, pad, left);
            break;
        case 'p':
            emit(s, '0'); emit(s, 'x');
            put_num(s, (uint64_t)va_arg(ap, void *), 0, 16, 0, 16, '0', 0);
            break;
        case '%': emit(s, '%'); break;
        case 0:   return;
        default:  emit(s, '%'); emit(s, *fmt);
        }
    }
}

void kvprintf(const char *fmt, va_list ap) {
    struct sink s = { put_term, 0, 0, 0 };
    format(&s, fmt, ap);
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct sink s = { put_buf, buf, size, 0 };
    format(&s, fmt, ap);
    if (size) buf[s.len < size ? s.len : size - 1] = 0;
    return (int)s.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
