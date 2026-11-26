#include "timer.h"
#include "io.h"

// CMOS：主機板上一小塊靠電池保存的記憶體，RTC 時間就放在裡面。
// 先把暫存器編號寫到 0x70，再從 0x71 讀值。
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int updating(void) { return cmos(0x0A) & 0x80; }   // RTC 正在換秒

static void read_raw(struct datetime *t) {
    while (updating()) {}
    t->second = cmos(0x00);
    t->minute = cmos(0x02);
    t->hour   = cmos(0x04);
    t->day    = cmos(0x07);
    t->month  = cmos(0x08);
    t->year   = cmos(0x09);
}

static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }   // 0x59 → 59

void rtc_read(struct datetime *t) {
    // 換秒的瞬間可能讀到一半新一半舊，所以讀到兩次一樣才算數
    struct datetime a, b;
    read_raw(&b);
    do {
        a = b;
        read_raw(&b);
    } while (a.second != b.second || a.minute != b.minute || a.hour != b.hour ||
             a.day != b.day || a.month != b.month || a.year != b.year);

    uint8_t status_b = cmos(0x0B);
    if (!(status_b & 0x04)) {              // bit 2 = 0：數值是 BCD 格式
        b.second = bcd(b.second);
        b.minute = bcd(b.minute);
        b.hour   = bcd(b.hour & 0x7F) | (b.hour & 0x80);
        b.day    = bcd(b.day);
        b.month  = bcd(b.month);
        b.year   = bcd(b.year);
    }
    if (!(status_b & 0x02) && (b.hour & 0x80))   // 12 小時制的下午
        b.hour = ((b.hour & 0x7F) + 12) % 24;
    b.year += 2000;
    *t = b;
}
