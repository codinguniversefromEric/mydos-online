#pragma once
#include <stdint.h>
#include "panic.h"

// MYDOS 跟 DOS 一樣一次只跑一支程式：命令列呼叫 process_run，
// 程式結束（或當掉、被 Ctrl+C）後才回到命令列。
//
// 使用者程式的記憶體配置（低半部，核心碰不到的那一半）：
//   0x0000000000400000  程式碼與資料（ELF 指定）
//   0x00007FFFFFFE0000  堆疊底
//   0x00007FFFFFFF0000  堆疊頂（往下長，64 KB）
#define USER_BASE        0x0000000000400000ULL
#define USER_STACK_TOP   0x00007FFFFFFF0000ULL
#define USER_STACK_SIZE  (64 * 1024)
#define USER_LIMIT       0x0000800000000000ULL   // 低半部的上限

// 執行程式；回傳結束碼。載入失敗回傳負數並印出原因
int  process_run(const char *path, const char *args);
int  process_running(void);

// 結束目前程式並回到命令列（從系統呼叫或例外處理裡呼叫）
__attribute__((noreturn)) void process_exit(int code);
// 程式在 ring 3 觸發 CPU 例外：印訊息、結束程式，系統繼續跑
__attribute__((noreturn)) void process_fault(int vec, const char *name, uint64_t err,
                                             struct int_frame *f);

// 檢查使用者給的指標是不是真的指向它自己的記憶體
int user_ptr_ok(const void *p, uint64_t len);

// Ctrl+C：鍵盤中斷設旗標，回到 ring 3 前檢查
extern volatile int process_break;
void process_check_break(struct int_frame *f);

void syscall_init(void);
