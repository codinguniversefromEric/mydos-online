#include "serial.h"
#include "io.h"
#include "kprintf.h"

#define COM1 0x3F8
#define LSR_DATA_READY 0x01      // 線路狀態暫存器：收到資料
#define LSR_TX_EMPTY   0x20      //                  可以送下一個

void serial_init(void) {
    outb(COM1 + 1, 0x00);        // 關掉序列埠中斷（我們用輪詢）
    outb(COM1 + 3, 0x80);        // 接下來設定鮑率
    outb(COM1 + 0, 0x03);        // 115200 / 3 = 38400 bps
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);        // 8 bit、無同位、1 stop bit
    outb(COM1 + 2, 0xC7);        // 開啟並清空 FIFO
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & LSR_TX_EMPTY)) {}
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) { while (*s) serial_putc(*s++); }

int serial_getc(void) {
    if (!(inb(COM1 + 5) & LSR_DATA_READY)) return -1;
    return inb(COM1);
}

// ESC [ 2 J = 清畫面，ESC [ H = 游標回左上角
void serial_ansi_clear(void) { serial_puts("\x1b[2J\x1b[H"); }

void serial_ansi_goto(int col, int row) {
    char buf[24];
    ksnprintf(buf, sizeof(buf), "\x1b[%d;%dH", row + 1, col + 1);   // ANSI 從 1 開始數
    serial_puts(buf);
}

// 24-bit 真彩色：ESC [ 38;2;R;G;B m（前景）、ESC [ 48;2;R;G;B m（背景）
void serial_ansi_color(uint32_t fg, uint32_t bg) {
    char buf[64];
    ksnprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm",
              (fg >> 16) & 255, (fg >> 8) & 255, fg & 255,
              (bg >> 16) & 255, (bg >> 8) & 255, bg & 255);
    serial_puts(buf);
}
