#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SECTOR_SIZE 512
#define SECTOR_COUNT 2880
#define IMAGE_SIZE (SECTOR_SIZE * SECTOR_COUNT)
#define ROOT_LBA 19
#define DATA_LBA 33

static uint8_t image[IMAGE_SIZE];

static void write_u16(uint8_t* destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void set_fat12_entry(uint8_t* fat, uint16_t cluster, uint16_t value) {
    const uint32_t offset = cluster + cluster / 2;
    if (cluster & 1) {
        fat[offset] = (uint8_t)((fat[offset] & 0x0F) | (value << 4));
        fat[offset + 1] = (uint8_t)(value >> 4);
    } else {
        fat[offset] = (uint8_t)value;
        fat[offset + 1] = (uint8_t)((fat[offset + 1] & 0xF0) | (value >> 8));
    }
}

static void make_image(void) {
    uint8_t* boot = image;
    boot[0] = 0xEB;
    boot[1] = 0x3C;
    boot[2] = 0x90;
    memcpy(boot + 3, "NEUTRA  ", 8);
    write_u16(boot + 11, SECTOR_SIZE);
    boot[13] = 1;
    write_u16(boot + 14, 1);
    boot[16] = 2;
    write_u16(boot + 17, 224);
    write_u16(boot + 19, SECTOR_COUNT);
    boot[21] = 0xF0;
    write_u16(boot + 22, 9);
    write_u16(boot + 24, 18);
    write_u16(boot + 26, 2);
    boot[38] = 0x29;
    write_u32(boot + 39, 0x4E455554);
    memcpy(boot + 43, "NEUTRA FAT ", 11);
    memcpy(boot + 54, "FAT12   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;

    const uint8_t fat_header[] = {0xF0, 0xFF, 0xFF, 0xF8, 0x0F, 0xFF};
    memcpy(image + SECTOR_SIZE, fat_header, sizeof(fat_header));
    memcpy(image + 10 * SECTOR_SIZE, fat_header, sizeof(fat_header));
    for (uint16_t cluster = 3; cluster <= 7; cluster++) {
        const uint16_t next = cluster == 3 || cluster == 7 ? 0x0FFF : cluster + 1;
        set_fat12_entry(image + SECTOR_SIZE, cluster, next);
        set_fat12_entry(image + 10 * SECTOR_SIZE, cluster, next);
    }

    const char contents[] = "hello i am dev this is code: printf('hello os'); in os\r\n";
    uint8_t* entry = image + ROOT_LBA * SECTOR_SIZE;
    memcpy(entry, "HELLO   TXT", 11);
    entry[11] = 0x20;
    write_u16(entry + 26, 2);
    write_u32(entry + 28, sizeof(contents) - 1);
    memcpy(image + DATA_LBA * SECTOR_SIZE, contents, sizeof(contents) - 1);

    entry += 32;
    memcpy(entry, "KERNEL_M   ", 11);
    entry[11] = 0x10;
    write_u16(entry + 26, 3);

    const char note[] = "do what you want\r\n";
    entry = image + (DATA_LBA + 1) * SECTOR_SIZE;
    memcpy(entry, "NOTES   TXT", 11);
    entry[11] = 0x20;
    write_u16(entry + 26, 4);
    write_u32(entry + 28, sizeof(note) - 1);
    memcpy(image + (DATA_LBA + 2) * SECTOR_SIZE, note, sizeof(note) - 1);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: mkfat12 output-image\n");
        return 2;
    }

    make_image();
    FILE* output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror("mkfat12: fopen");
        return 1;
    }
    const size_t written = fwrite(image, 1, sizeof(image), output);
    const int close_result = fclose(output);
    if (written != sizeof(image) || close_result != 0) {
        fprintf(stderr, "mkfat12: failed to write image\n");
        return 1;
    }
    return 0;
}
