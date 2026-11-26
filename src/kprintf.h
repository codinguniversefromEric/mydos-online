#pragma once
#include <stdarg.h>
#include <stddef.h>

// 支援：%c %s %d %i %u %x %X %p %%
// 旗標：0（補零）、-（靠左）；寬度；長度 l / ll
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

// 印到字串，最多 size-1 個字元，一定以 \0 結尾；回傳想印的總長度
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
