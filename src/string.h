#pragma once
#include <stddef.h>
#include <stdint.h>

// 核心沒有 C 標準函式庫，常用的字串／記憶體函式自己寫。
// GCC 即使在 -ffreestanding 下也可能自動呼叫 memcpy/memset，所以這四個一定要有。
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    stricmp(const char *a, const char *b);        // 不分大小寫
char  *strcpy(char *dst, const char *src);
char  *strncpy0(char *dst, const char *src, size_t n); // 一定以 \0 結尾
char  *strcat(char *dst, const char *src);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char   toupper(char c);
long   atol(const char *s);
