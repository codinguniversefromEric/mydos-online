#pragma once
#include <stdint.h>

// COM1 序列埠。輸出會同步顯示在 WSL 終端機或網頁版的終端機；
// 輸入也能當成鍵盤用（網頁版沒有 PS/2 鍵盤，全靠這條線）。
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
int  serial_getc(void);          // 有收到字元就回傳，沒有回傳 -1

// 用 ANSI 跳脫碼讓遠端終端機跟著畫面動作
void serial_ansi_clear(void);
void serial_ansi_goto(int col, int row);
void serial_ansi_color(uint32_t fg, uint32_t bg);
