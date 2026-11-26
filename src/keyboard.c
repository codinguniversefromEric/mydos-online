#include "keyboard.h"
#include "irq.h"
#include "io.h"
#include "process.h"
#include "serial.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

// Scancode set 1（QEMU 和大部分 BIOS 預設會轉成這一套）。
// 陣列索引是按下時的 scancode，0 代表「不是可印字元」。
static const char keymap[58] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
};
static const char keymap_shift[58] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ',
};

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A
#define SC_EXT    0xE0   // 延伸鍵前綴（方向鍵、右 Ctrl 等）

static int shift, ctrl, caps, extended;

// 環狀緩衝區：中斷放進來，keyboard_getc 拿出去
#define BUF_SIZE 128
static volatile char buf[BUF_SIZE];
static volatile unsigned head, tail;

static void push(char c) {
    unsigned next = (head + 1) % BUF_SIZE;
    if (next != tail) {          // 滿了就丟掉，不覆蓋舊字
        buf[head] = c;
        head = next;
    }
}

static void deliver(int c);

static void keyboard_irq(void) {
    uint8_t sc = inb(KBD_DATA);  // 一定要讀，否則控制器不會送下一個

    if (sc == SC_EXT) { extended = 1; return; }
    int released = sc & 0x80;    // bit 7 = 放開
    uint8_t key = sc & 0x7F;

    if (extended) {              // 延伸鍵：右 Ctrl 和方向鍵
        extended = 0;
        if (key == SC_CTRL) { ctrl = !released; return; }
        if (released) return;
        switch (key) {
        case 0x48: push(KEY_UP);    break;
        case 0x50: push(KEY_DOWN);  break;
        case 0x4B: push(KEY_LEFT);  break;
        case 0x4D: push(KEY_RIGHT); break;
        }
        return;
    }

    switch (key) {
    case SC_LSHIFT: case SC_RSHIFT: shift = !released; return;
    case SC_CTRL:                   ctrl  = !released; return;
    case SC_CAPS:   if (!released) caps = !caps;       return;
    }
    if (!released && key == 0x3D) { push(KEY_F3); return; }   // F3：重打上一個指令
    if (released || key >= sizeof(keymap)) return;

    char c = shift ? keymap_shift[key] : keymap[key];
    if (!c) return;
    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;                       // Caps Lock 只反轉字母大小寫
    if (ctrl && (c | 0x20) >= 'a' && (c | 0x20) <= 'z')
        c = (c | 0x20) - 'a' + 1;        // Ctrl+C = 3、Ctrl+L = 12……
    deliver(c);
}

// 8042 控制器：寫命令前要等輸入緩衝區空，讀資料前要等輸出緩衝區滿
static void ctl_wait_write(void) { for (int i = 0; i < 100000 && (inb(KBD_STATUS) & 2); i++) {} }
static void ctl_wait_read(void)  { for (int i = 0; i < 100000 && !(inb(KBD_STATUS) & 1); i++) {} }
static void ctl_cmd(uint8_t c)   { ctl_wait_write(); outb(KBD_STATUS, c); }

// 一個按鍵進緩衝區（PS/2 和序列埠共用）
static void deliver(int c) {
    if (c == 3 && process_running())
        process_break = 1;
    push((char)c);
}

// ---------------------------------------------------------------------
//  序列埠當鍵盤：網頁上的終端機（xterm.js）送來的是文字，不是 scancode。
//  Enter 是 \r、Backspace 是 0x7F、方向鍵是「ESC [ A」這種三個字元的序列。
// ---------------------------------------------------------------------
static int esc_state;       // 0 = 一般、1 = 收到 ESC、2 = 收到 ESC [
static int esc_age;         // ESC 之後等了幾次輪詢

void keyboard_serial_poll(void) {
    int c;
    while ((c = serial_getc()) >= 0) {
        if (esc_state == 1) {
            if (c == '[' || c == 'O') { esc_state = 2; continue; }
            esc_state = 0;
            deliver(27);                       // 單獨的 Esc 鍵
        } else if (esc_state == 2) {
            esc_state = 0;
            switch (c) {
            case 'A': deliver(KEY_UP);    break;
            case 'B': deliver(KEY_DOWN);  break;
            case 'C': deliver(KEY_RIGHT); break;
            case 'D': deliver(KEY_LEFT);  break;
            case 'R': deliver(KEY_F3);    break;   // F3 = ESC O R
            }
            continue;
        }
        if (c == 27) { esc_state = 1; esc_age = 0; continue; }
        if (c == '\r') c = '\n';
        else if (c == 0x7F) c = '\b';
        else if (c == '\n') continue;           // \r\n 只算一次
        deliver(c);
    }
    // ESC 後面 30 ms 都沒有字：那就是使用者真的按了 Esc
    if (esc_state == 1 && ++esc_age > 3) { esc_state = 0; deliver(27); }
}

void keyboard_init(void) {
    // 不要相信開機程式留下的設定：自己把控制器設成我們要的樣子
    ctl_cmd(0xAD);                       // 先關掉鍵盤埠，設定期間不要有按鍵進來
    while (inb(KBD_STATUS) & 1)          // 丟掉殘留的位元組
        inb(KBD_DATA);

    ctl_cmd(0x20);                       // 讀設定位元組
    ctl_wait_read();
    uint8_t cfg = inb(KBD_DATA);
    cfg |= 0x01;                         // bit 0：鍵盤埠要發 IRQ1
    cfg |= 0x40;                         // bit 6：轉成 scancode set 1
    cfg &= ~0x10;                        // bit 4：清掉 = 鍵盤時脈開啟
    ctl_cmd(0x60);                       // 寫回設定位元組
    ctl_wait_write();
    outb(KBD_DATA, cfg);

    ctl_cmd(0xAE);                       // 重新打開鍵盤埠
    irq_install(1, keyboard_irq);
}

int keyboard_poll(void) {
    int c = -1;
    irq_disable();
    if (head != tail) {
        c = (unsigned char)buf[tail];
        tail = (tail + 1) % BUF_SIZE;
    }
    irq_enable();
    return c;
}

void keyboard_flush(void) {
    irq_disable();
    tail = head;
    irq_enable();
}

int keyboard_getc(void) {
    for (;;) {
        irq_disable();               // 檢查和睡覺之間不能被中斷插隊
        if (head != tail) {
            int c = (unsigned char)buf[tail];
            tail = (tail + 1) % BUF_SIZE;
            irq_enable();
            return c;
        }
        // sti 要等下一條指令才生效，所以 sti; hlt 之間不會漏掉中斷
        __asm__ volatile ("sti; hlt");
    }
}
