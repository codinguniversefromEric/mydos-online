#pragma once

// 不是 ASCII 的特殊鍵，用 0x80 以上的值表示
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_F3    0x84

void keyboard_init(void);   // 清空控制器、註冊 IRQ1
int  keyboard_getc(void);   // 等到有字才回傳（等待時 CPU 會休息）
int  keyboard_poll(void);   // 有字就回傳，沒有回傳 -1，不等待
void keyboard_flush(void);  // 丟掉緩衝區裡還沒讀的字
void keyboard_serial_poll(void);   // 把序列埠收到的字當成按鍵（計時器每 10 ms 呼叫）
