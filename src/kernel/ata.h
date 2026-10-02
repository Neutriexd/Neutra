#ifndef ATA_H
#define ATA_H

#include <stdint.h>

int ATA_Identify(uint32_t* sectorCount);
int ATA_ReadSector(uint32_t lba, void* buffer);
int ATA_WriteSector(uint32_t lba, const void* buffer);

#endif