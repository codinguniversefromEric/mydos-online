#include "ata.h"
#include "io.h"
#include "string.h"

// 主通道的 I/O port：0x1F0–0x1F7 是命令區，0x3F6 是控制暫存器
#define ATA_DATA     0x1F0   // 16-bit 資料，一次搬 2 byte
#define ATA_ERROR    0x1F1
#define ATA_COUNT    0x1F2   // 要讀幾個磁區
#define ATA_LBA0     0x1F3   // LBA bit 0–7
#define ATA_LBA1     0x1F4   //     bit 8–15
#define ATA_LBA2     0x1F5   //     bit 16–23
#define ATA_DRIVE    0x1F6   // bit 0–3 = LBA bit 24–27，0xE0 = LBA 模式 + 主碟
#define ATA_CMD      0x1F7   // 寫：命令；讀：狀態
#define ATA_CTRL     0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08         // 資料準備好了，可以搬
#define ST_DF   0x20         // 硬碟故障
#define ST_BSY  0x80         // 忙碌中，其他位元都不可信

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

static uint32_t total_sectors;
static char model[41];

// 讀 4 次控制暫存器 ≈ 400 ns，讓硬碟有時間更新狀態
static void delay400(void) { for (int i = 0; i < 4; i++) inb(ATA_CTRL); }

static int wait_ready(int need_drq) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t st = inb(ATA_CMD);
        if (st & ST_BSY) continue;
        if (st & (ST_ERR | ST_DF)) return -1;
        if (!need_drq || (st & ST_DRQ)) return 0;
    }
    return -1;   // 逾時
}

int ata_init(void) {
    outb(ATA_CTRL, 0x02);            // nIEN：不要發中斷，我們用輪詢
    outb(ATA_DRIVE, 0xA0);           // 選主碟
    delay400();
    outb(ATA_COUNT, 0); outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_CMD, CMD_IDENTIFY);
    if (inb(ATA_CMD) == 0) return -1;              // 狀態全 0：沒有硬碟
    for (int i = 0; i < 1000000 && (inb(ATA_CMD) & ST_BSY); i++) {}
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) return -1; // 不是 ATA（例如光碟機）
    if (wait_ready(1)) return -1;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);
    total_sectors = id[60] | ((uint32_t)id[61] << 16);   // LBA28 可用磁區數
    for (int i = 0; i < 20; i++) {                       // 型號字串，每 2 byte 前後顛倒
        model[i * 2]     = id[27 + i] >> 8;
        model[i * 2 + 1] = id[27 + i] & 0xFF;
    }
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
    return 0;
}

static void setup(uint32_t lba, uint8_t count, uint8_t cmd) {
    wait_ready(0);
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COUNT, count);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_CMD, cmd);
}

int ata_read(uint32_t lba, uint32_t count, void *buf) {
    uint16_t *p = buf;
    while (count) {
        uint8_t n = count > 255 ? 255 : count;   // 一次命令最多 255 磁區
        setup(lba, n, CMD_READ);
        for (int s = 0; s < n; s++) {
            delay400();
            if (wait_ready(1)) return -1;
            for (int i = 0; i < 256; i++) *p++ = inw(ATA_DATA);
        }
        lba += n; count -= n;
    }
    return 0;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf) {
    const uint16_t *p = buf;
    while (count) {
        uint8_t n = count > 255 ? 255 : count;
        setup(lba, n, CMD_WRITE);
        for (int s = 0; s < n; s++) {
            delay400();
            if (wait_ready(1)) return -1;
            for (int i = 0; i < 256; i++) outw(ATA_DATA, *p++);
        }
        lba += n; count -= n;
    }
    outb(ATA_CMD, CMD_FLUSH);     // 把硬碟內部快取寫進碟片，關機才不會掉資料
    return wait_ready(0);
}

uint32_t ata_sectors(void) { return total_sectors; }
const char *ata_model(void) { return model; }
