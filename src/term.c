#include "term.h"
#include "serial.h"
#include "font8x8_basic.h"
#define SCALE 2
#define CELL_W (8 * SCALE)
#define CELL_H (8 * SCALE)
#define MARGIN 8
#define STATUS_BG 0x008080
static uint8_t *fb_base; static uint64_t fb_pitch, fb_w, fb_h;
static uint64_t cols, rows, cx, cy, top_px;
static uint32_t fg = 0xC0C0C0, bg = 0x000000;
static inline uint32_t *row_ptr(uint64_t y) { return (uint32_t *)(fb_base + y * fb_pitch); }
static void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t c) {
    for (uint64_t j = 0; j < h; j++) { uint32_t *p = row_ptr(y + j); for (uint64_t i = 0; i < w; i++) p[x + i] = c; }
}
static uint64_t cell_x(uint64_t c) { return MARGIN + c * CELL_W; }
static uint64_t cell_y(uint64_t r) { return top_px + r * CELL_H; }
static void draw_char(uint64_t px, uint64_t py, char ch, uint32_t f, uint32_t b) {
    unsigned c = (unsigned char)ch; if (c > 127) c = '?';
    for (int row = 0; row < 8; row++) {
        uint8_t bits = (uint8_t)font8x8_basic[c][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (1 << col)) ? f : b;
            for (int sy = 0; sy < SCALE; sy++) { uint32_t *p = row_ptr(py + row * SCALE + sy) + px + col * SCALE; for (int sx = 0; sx < SCALE; sx++) p[sx] = color; }
        }
    }
}
static void cursor(uint32_t c) { fill_rect(cell_x(cx), cell_y(cy) + CELL_H - 3, CELL_W, 3, c); }
static void recalc(void) { cols = (fb_w - 2 * MARGIN) / CELL_W; rows = (fb_h - top_px) / CELL_H; }
void term_init(struct limine_framebuffer *fb) {
    fb_base = fb->address; fb_pitch = fb->pitch; fb_w = fb->width; fb_h = fb->height;
    top_px = 0; recalc(); fill_rect(0, 0, fb_w, fb_h, bg); cx = cy = 0;
}
void term_status(const char *text) {
    fill_rect(0, 0, fb_w, CELL_H + 8, STATUS_BG);
    for (uint64_t i = 0; text[i]; i++) draw_char(MARGIN + i * CELL_W, 4, text[i], 0xFFFFFF, STATUS_BG);
    top_px = CELL_H + 12; recalc(); cx = cy = 0;
}
void term_set_color(uint32_t f, uint32_t b) { fg = f; bg = b; serial_ansi_color(f, b); }
void term_get_color(uint32_t *f, uint32_t *b) { *f = fg; *b = bg; }
int term_cols(void) { return (int)cols; }
int term_rows(void) { return (int)rows; }

void term_goto(int col, int row) {
    if (col < 0 || row < 0 || (uint64_t)col >= cols || (uint64_t)row >= rows) return;
    cursor(bg);
    cx = col;
    cy = row;
    cursor(fg);
    serial_ansi_goto(col, row);
}

// 可能在計時器中斷裡被呼叫：只畫狀態列那一塊，不碰游標和文字區
void term_status_right(const char *text) {
    uint64_t n = 0;
    while (text[n]) n++;
    uint64_t x = fb_w - MARGIN - n * CELL_W;
    for (uint64_t i = 0; i < n; i++)
        draw_char(x + i * CELL_W, 4, text[i], 0xFFFFFF, STATUS_BG);
}
void term_clear(void) {
    fill_rect(0, top_px, fb_w, fb_h - top_px, bg);
    cx = cy = 0;
    cursor(fg);
    serial_ansi_clear();
}
static void scroll(void) {
    uint8_t *dst = fb_base + top_px * fb_pitch, *src = dst + CELL_H * fb_pitch;
    uint64_t total = (rows - 1) * CELL_H * fb_pitch, n8 = total / 8;
    uint64_t *d8 = (uint64_t *)dst, *s8 = (uint64_t *)src;
    for (uint64_t i = 0; i < n8; i++) d8[i] = s8[i];
    for (uint64_t i = n8 * 8; i < total; i++) dst[i] = src[i];
    fill_rect(0, cell_y(rows - 1), fb_w, CELL_H, bg);
}
static void newline(void) { cx = 0; if (++cy >= rows) { scroll(); cy = rows - 1; } }
void term_putc(char c) {
    if (c == '\n') serial_putc('\r');
    if (c == '\b') serial_puts("\b \b");    // 遠端終端機的退格只移游標，要自己蓋掉
    else serial_putc(c);
    cursor(bg);
    switch (c) {
    case '\n': newline(); break;
    case '\r': cx = 0; break;
    case '\b': if (cx > 0) { cx--; draw_char(cell_x(cx), cell_y(cy), ' ', fg, bg); } break;
    case '\t': cx = (cx + 8) & ~7ULL; if (cx >= cols) newline(); break;
    default: draw_char(cell_x(cx), cell_y(cy), c, fg, bg); if (++cx >= cols) newline();
    }
    cursor(fg);
}
