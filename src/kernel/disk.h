#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define far

typedef struct {
    uint8_t* Image;
    uint32_t SectorCount;
} DISK;

bool DISK_Initialize(DISK* disk, void* image, uint32_t imageSize);
bool DISK_ReadSectors(DISK* disk, uint32_t lba, uint8_t sectors, void far* dataOut);
bool DISK_WriteSectors(DISK* disk, uint32_t lba, uint8_t sectors, const void* dataIn);