#pragma once
#include <stdint.h>

// CPU 進中斷時自動推進堆疊的五個值（由低位址到高位址）
struct int_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

// 印出藍色當機畫面並永久停機
__attribute__((noreturn)) void panic(const char *fmt, ...);

// CPU 例外專用：多印出暫存器資訊
__attribute__((noreturn)) void panic_exception(int vec, uint64_t err, int has_err,
                                              struct int_frame *f);
