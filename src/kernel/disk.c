#include "disk.h"
#include "ata.h"

#define SECTOR_SIZE 512

#define DISK_IS_ATA(d) ((d)->Image == NULL)

bool DISK_Initialize(DISK* disk, void* image, uint32_t imageSize)
{
    if (disk == NULL || image == NULL || imageSize < SECTOR_SIZE ||
        imageSize % SECTOR_SIZE != 0) {
        return false;
    }

    disk->Image = (uint8_t*)image;
    disk->SectorCount = imageSize / SECTOR_SIZE;
    return true;
}

bool DISK_InitializeAta(DISK* disk)
{
    uint32_t sectors;
    if (disk == NULL || !ATA_Identify(&sectors)) return false;

    disk->Image = NULL;
    disk->SectorCount = sectors;
    return true;
}

bool DISK_ReadSectors(DISK* disk, uint32_t lba, uint8_t sectors, void far* dataOut)
{
    if (disk == NULL || dataOut == NULL || sectors == 0 ||
        lba >= disk->SectorCount || sectors > disk->SectorCount - lba) {
        return false;
    }

    uint8_t* destination = (uint8_t*)dataOut;
    if (DISK_IS_ATA(disk)) {
        for (uint32_t i = 0; i < sectors; i++) {
            if (!ATA_ReadSector(lba + i, destination + i * SECTOR_SIZE)) return false;
        }
        return true;
    }

    const uint8_t* source = disk->Image + lba * SECTOR_SIZE;
    const uint32_t byteCount = (uint32_t)sectors * SECTOR_SIZE;
    for (uint32_t i = 0; i < byteCount; i++) {
        destination[i] = source[i];
    }
    return true;
}

bool DISK_WriteSectors(DISK* disk, uint32_t lba, uint8_t sectors, const void* dataIn)
{
    if (disk == NULL || dataIn == NULL || sectors == 0 ||
        lba >= disk->SectorCount || sectors > disk->SectorCount - lba) {
        return false;
    }

    const uint8_t* source = (const uint8_t*)dataIn;
    if (DISK_IS_ATA(disk)) {
        for (uint32_t i = 0; i < sectors; i++) {
            if (!ATA_WriteSector(lba + i, source + i * SECTOR_SIZE)) return false;
        }
        return true;
    }

    uint8_t* destination = disk->Image + lba * SECTOR_SIZE;
    const uint32_t byteCount = (uint32_t)sectors * SECTOR_SIZE;
    for (uint32_t i = 0; i < byteCount; i++) {
        destination[i] = source[i];
    }
    return true;
}