#include "ata.h"

#define ATA_IO   0x1F0
#define ATA_CTRL 0x3F6

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

static inline uint8_t ata_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ata_outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t ata_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ata_outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ata_delay(void) {
    for (int i = 0; i < 4; i++) ata_inb(ATA_CTRL);
}

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if (!(ata_inb(ATA_IO + 7) & ST_BSY)) return 1;
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        const uint8_t status = ata_inb(ATA_IO + 7);
        if (status & ST_BSY) continue;
        if (status & (ST_ERR | ST_DF)) return 0;
        if (status & ST_DRQ) return 1;
    }
    return 0;
}

static void ata_setup(uint32_t lba, uint8_t command) {
    ata_outb(ATA_IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_outb(ATA_IO + 2, 1);
    ata_outb(ATA_IO + 3, (uint8_t)lba);
    ata_outb(ATA_IO + 4, (uint8_t)(lba >> 8));
    ata_outb(ATA_IO + 5, (uint8_t)(lba >> 16));
    ata_outb(ATA_IO + 7, command);
}

int ATA_Identify(uint32_t* sectorCount) {
    if (sectorCount == 0) return 0;
    if (ata_inb(ATA_IO + 7) == 0xFF) return 0;

    ata_outb(ATA_IO + 6, 0xA0);
    ata_delay();
    ata_outb(ATA_IO + 2, 0);
    ata_outb(ATA_IO + 3, 0);
    ata_outb(ATA_IO + 4, 0);
    ata_outb(ATA_IO + 5, 0);
    ata_outb(ATA_IO + 7, 0xEC);
    if (ata_inb(ATA_IO + 7) == 0) return 0;
    if (!ata_wait_not_busy()) return 0;
    if (ata_inb(ATA_IO + 4) != 0 || ata_inb(ATA_IO + 5) != 0) return 0;
    if (!ata_wait_drq()) return 0;

    uint16_t identify[256];
    for (int i = 0; i < 256; i++) identify[i] = ata_inw(ATA_IO);

    const uint32_t total = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    if (total == 0) return 0;
    *sectorCount = total;
    return 1;
}

int ATA_ReadSector(uint32_t lba, void* buffer) {
    if (buffer == 0 || lba >= 0x10000000u) return 0;
    if (!ata_wait_not_busy()) return 0;
    ata_setup(lba, 0x20);
    ata_delay();
    if (!ata_wait_drq()) return 0;

    uint16_t* words = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) words[i] = ata_inw(ATA_IO);
    return 1;
}

int ATA_WriteSector(uint32_t lba, const void* buffer) {
    if (buffer == 0 || lba >= 0x10000000u) return 0;
    if (!ata_wait_not_busy()) return 0;
    ata_setup(lba, 0x30);
    ata_delay();
    if (!ata_wait_drq()) return 0;

    const uint16_t* words = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) ata_outw(ATA_IO, words[i]);

    ata_outb(ATA_IO + 7, 0xE7);
    return ata_wait_not_busy();
}