#pragma once
#include <stdint.h>
#include <limine.h>
void term_init(struct limine_framebuffer *fb);
void term_status(const char *text);
void term_set_color(uint32_t fg, uint32_t bg);
void term_putc(char c);
void term_clear(void);
void term_status_right(const char *text);   // 狀態列右邊（時鐘用），不動游標
void term_get_color(uint32_t *fg, uint32_t *bg);
int  term_cols(void);
int  term_rows(void);
void term_goto(int col, int row);          // 移動游標（給全螢幕程式用）
