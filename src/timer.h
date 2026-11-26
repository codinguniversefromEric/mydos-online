#pragma once
#include <stdint.h>

#define TIMER_HZ 100                  // 每秒 100 次 → 一個 tick = 10 ms

void     timer_init(void);
uint64_t timer_ticks(void);           // 開機以來的 tick 數
void     timer_sleep_ms(uint64_t ms);

// ---- RTC：主機板上靠電池走的時鐘（rtc.c）----
struct datetime { int year, month, day, hour, minute, second; };
void rtc_read(struct datetime *dt);
