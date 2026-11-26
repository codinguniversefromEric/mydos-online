#pragma once
// 系統呼叫編號：核心（syscall.c）和使用者函式庫（user/lib）共用這個檔案，
// 兩邊才不會對不上。

#define SYS_EXIT      0    // exit(code)
#define SYS_PUTS      1    // puts(buf, len)        印到螢幕
#define SYS_GETKEY    2    // getkey()              等一個按鍵
#define SYS_POLLKEY   3    // pollkey()             有按鍵就回傳，沒有回傳 -1
#define SYS_READLINE  4    // readline(buf, max)    讀一行（有回顯、Backspace）
#define SYS_OPEN      5    // open(path, mode)      回傳檔案代號
#define SYS_READ      6    // read(fd, buf, len)
#define SYS_WRITE     7    // write(fd, buf, len)
#define SYS_CLOSE     8    // close(fd)             寫入模式在這時存檔
#define SYS_DELETE    9    // delete(path)
#define SYS_TICKS    10    // ticks()               開機以來的毫秒數
#define SYS_SLEEP    11    // sleep(ms)
#define SYS_TIME     12    // time(struct datetime*)
#define SYS_CLS      13    // cls()
#define SYS_COLOR    14    // color(fg_rgb, bg_rgb)
#define SYS_SCREEN   15    // screen()              回傳 (欄數 << 32) | 列數
#define SYS_GOTO     16    // gotoxy(col, row)

#define MYDOS_O_READ  0
#define MYDOS_O_WRITE 1
