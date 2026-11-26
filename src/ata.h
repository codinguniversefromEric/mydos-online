#pragma once
#include <stdint.h>

#define SECTOR_SIZE 512

// 主通道（primary）主碟（master）上的 ATA 硬碟，用最簡單的 PIO 模式
int      ata_init(void);                                         // 0 = 找到硬碟
int      ata_read(uint32_t lba, uint32_t count, void *buf);       // 0 = 成功
int      ata_write(uint32_t lba, uint32_t count, const void *buf);
uint32_t ata_sectors(void);
const char *ata_model(void);
