#include "string.h"

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }   // 重疊時從尾巴搬
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n--; x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (uint8_t)*a - (uint8_t)*b; if (!*a) return 0; }
    return 0;
}
char toupper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int stricmp(const char *a, const char *b) {
    while (*a && toupper(*a) == toupper(*b)) { a++; b++; }
    return (uint8_t)toupper(*a) - (uint8_t)toupper(*b);
}
char *strcpy(char *dst, const char *src) { char *d = dst; while ((*d++ = *src++)) {} return dst; }
char *strncpy0(char *dst, const char *src, size_t n) {
    size_t i = 0;
    if (!n) return dst;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return dst;
}
char *strcat(char *dst, const char *src) { strcpy(dst + strlen(dst), src); return dst; }
char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c ? 0 : (char *)s;
}
char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (; *s; s++) if (*s == (char)c) r = s;
    return (char *)r;
}
long atol(const char *s) {
    long v = 0; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
