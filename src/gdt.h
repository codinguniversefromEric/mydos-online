#pragma once
#include <stdint.h>

// GDT 的排列（每格 8 byte，TSS 佔兩格）
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

// 給 ring 3 用的選擇子：低 2 bit 是「要求的權限等級」(RPL) = 3
#define USER_CS (GDT_USER_CODE | 3)   // 0x23
#define USER_SS (GDT_USER_DATA | 3)   // 0x1B

void gdt_init(void);
void tss_set_kernel_stack(uint64_t rsp0);
