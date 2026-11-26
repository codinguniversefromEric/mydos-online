#pragma once
// MYDOS 程式用的迷你 C 函式庫。
// 這裡的每個函式最後都變成一個 int 0x80 系統呼叫，交給核心去做。
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "../../src/syscall_nums.h"

// ---- 系統呼叫 ----
__attribute__((noreturn)) void exit(int code);
int   getkey(void);                     // 等一個按鍵
int   pollkey(void);                    // 沒按鍵回傳 -1
int   readline(char *buf, int max);
int   open(const char *path, int mode); // MYDOS_O_READ / MYDOS_O_WRITE
int   read(int fd, void *buf, int len);
int   write(int fd, const void *buf, int len);
int   close(int fd);
int   delete(const char *path);
uint64_t ticks(void);                   // 開機以來幾毫秒
void  sleep(int ms);
struct datetime { int year, month, day, hour, minute, second; };
void  gettime(struct datetime *t);
void  cls(void);
void  color(uint32_t fg, uint32_t bg);
int   screen_cols(void);
int   screen_rows(void);
void  gotoxy(int col, int row);

// ---- 輸出 ----
void  putchar(char c);
void  print(const char *s);
int   printf(const char *fmt, ...);     // %d %u %x %s %c %%，可加寬度、0、-
int   snprintf(char *buf, size_t n, const char *fmt, ...);

// ---- 字串與記憶體 ----
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
char  *strcpy(char *d, const char *s);
void  *memcpy(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    atoi(const char *s);
int    toupper(int c);

// ---- 記憶體配置（從程式自己的一塊靜態區切，結束時整塊還給系統）----
void  *malloc(size_t n);
void   free(void *p);

// ---- 亂數 ----
void     srand(uint32_t seed);
uint32_t rand(void);
