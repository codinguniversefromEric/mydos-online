#include "timer.h"
#include "irq.h"
#include "io.h"
#include "term.h"
#include "kprintf.h"
#include "keyboard.h"

// PIT（8253/8254）：一顆以 1,193,182 Hz 跳動的計數器。
// 設定「除數」N，它就每 N 下發一次 IRQ0 → 頻率 = 1193182 / N。
#define PIT_FREQ    1193182
#define PIT_CH0     0x40
#define PIT_CMD     0x43

static volatile uint64_t ticks;

static void update_clock(void) {
    struct datetime t;
    rtc_read(&t);
    char buf[24];
    ksnprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
              t.year, t.month, t.day, t.hour, t.minute, t.second);
    term_status_right(buf);
}

static void timer_irq(void) {
    ticks++;
    keyboard_serial_poll();                        // 序列埠也可以當鍵盤
    if (ticks % TIMER_HZ == 0) update_clock();   // 每秒更新狀態列時鐘
}

void timer_init(void) {
    uint16_t divisor = PIT_FREQ / TIMER_HZ;      // 11931
    outb(PIT_CMD, 0x36);       // 通道 0、先低後高位元組、模式 3（方波）
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, divisor >> 8);
    irq_install(0, timer_irq);
    update_clock();
}

uint64_t timer_ticks(void) { return ticks; }

void timer_sleep_ms(uint64_t ms) {
    uint64_t end = ticks + (ms * TIMER_HZ + 999) / 1000;
    while (ticks < end)
        __asm__ volatile ("sti; hlt");   // 睡到下一個中斷（最多 10 ms）
}
