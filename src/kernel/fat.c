#include "fat.h"
#include "graphics.h"

#define FAT_SECTOR_SIZE 512u
#define FAT_MAX_HANDLES 10
#define FAT_MAX_BYTES 65536u
#define FAT_ROOT_HANDLE (-1)
#define FAT_EOC 0x0FFFu
#define FAT_BAD_CLUSTER 0x0FF7u

#pragma pack(push, 1)
typedef struct {
    uint8_t Jump[3];
    uint8_t Oem[8];
    uint16_t BytesPerSector;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FatCount;
    uint16_t RootEntryCount;
    uint16_t TotalSectors16;
    uint8_t Media;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t TotalSectors32;
} FAT_BootSector;
#pragma pack(pop)

typedef struct {
    FAT_File Public;
    bool Opened;
    bool BufferValid;
    uint32_t FirstCluster;
    uint32_t CurrentCluster;
    uint32_t CurrentClusterIndex;
    uint32_t BufferSectorIndex;
    uint32_t BufferLba;
    uint32_t DirectoryEntryLba;
    uint16_t DirectoryEntryOffset;
    uint8_t Buffer[FAT_SECTOR_SIZE];
} FAT_FileData;

typedef struct {
    FAT_BootSector BootSector;
    FAT_FileData RootDirectory;
    FAT_FileData OpenedFiles[FAT_MAX_HANDLES];
} FAT_Data;

static FAT_Data g_Data;
static uint8_t g_Fat[FAT_MAX_BYTES];
static uint32_t g_FatSizeBytes;
static uint32_t g_RootDirectoryLba;
static uint32_t g_RootDirectorySectors;
static uint32_t g_DataSectionLba;
static uint32_t g_ClusterCount;
static uint8_t g_FatType;
static bool g_Initialized;

static void clear_bytes(void* destination, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    for (uint32_t i = 0; i < size; i++) bytes[i] = 0;
}

static void copy_bytes(void* destination, const void* source, uint32_t size) {
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;
    for (uint32_t i = 0; i < size; i++) out[i] = in[i];
}

static bool equal_bytes(const uint8_t* left, const uint8_t* right, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static uint16_t read_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t string_length(const char* string) {
    uint32_t length = 0;
    while (string[length]) length++;
    return length;
}

static bool power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return g_DataSectionLba + (cluster - 2) * g_Data.BootSector.SectorsPerCluster;
}

static uint32_t next_cluster(uint32_t cluster) {
    uint32_t index;
    if (g_FatType == 12) {
        index = cluster + cluster / 2;
        if (index + 1 >= g_FatSizeBytes) return FAT_BAD_CLUSTER;
        uint16_t value = read_u16(g_Fat + index);
        return (cluster & 1) ? value >> 4 : value & 0x0FFF;
    }

    index = cluster * 2;
    if (index + 1 >= g_FatSizeBytes) return FAT_BAD_CLUSTER;
    return read_u16(g_Fat + index);
}

static bool is_end_cluster(uint32_t cluster) {
    return g_FatType == 12 ? cluster >= 0x0FF8u : cluster >= 0xFFF8u;
}

static bool cluster_valid(uint32_t cluster) {
    return cluster >= 2 && cluster < g_ClusterCount + 2;
}

static bool read_file_sector(DISK* disk, FAT_FileData* file, uint32_t sectorIndex) {
    uint32_t lba;
    if (file->Public.Handle == FAT_ROOT_HANDLE) {
        if (sectorIndex >= g_RootDirectorySectors) return false;
        lba = g_RootDirectoryLba + sectorIndex;
    } else {
        const uint32_t sectorsPerCluster = g_Data.BootSector.SectorsPerCluster;
        const uint32_t targetClusterIndex = sectorIndex / sectorsPerCluster;
        const uint32_t sectorInCluster = sectorIndex % sectorsPerCluster;

        if (!cluster_valid(file->FirstCluster)) return false;
        if (targetClusterIndex < file->CurrentClusterIndex) {
            file->CurrentCluster = file->FirstCluster;
            file->CurrentClusterIndex = 0;
        }
        while (file->CurrentClusterIndex < targetClusterIndex) {
            const uint32_t next = next_cluster(file->CurrentCluster);
            if (is_end_cluster(next) || !cluster_valid(next) || next == FAT_BAD_CLUSTER) {
                return false;
            }
            file->CurrentCluster = next;
            file->CurrentClusterIndex++;
        }
        lba = cluster_to_lba(file->CurrentCluster) + sectorInCluster;
    }

    if (!DISK_ReadSectors(disk, lba, 1, file->Buffer)) return false;
    file->BufferSectorIndex = sectorIndex;
    file->BufferLba = lba;
    file->BufferValid = true;
    return true;
}

bool FAT_Initialize(DISK* disk) {
    uint8_t bootSector[FAT_SECTOR_SIZE];
    g_Initialized = false;
    if (disk == NULL || !DISK_ReadSectors(disk, 0, 1, bootSector)) return false;
    if (bootSector[510] != 0x55 || bootSector[511] != 0xAA) return false;

    clear_bytes(&g_Data, sizeof(g_Data));
    copy_bytes(&g_Data.BootSector, bootSector, sizeof(g_Data.BootSector));

    const uint32_t bytesPerSector = g_Data.BootSector.BytesPerSector;
    const uint32_t sectorsPerCluster = g_Data.BootSector.SectorsPerCluster;
    const uint32_t reservedSectors = g_Data.BootSector.ReservedSectors;
    const uint32_t fatCount = g_Data.BootSector.FatCount;
    const uint32_t sectorsPerFat = g_Data.BootSector.SectorsPerFat;
    const uint32_t totalSectors = g_Data.BootSector.TotalSectors16 != 0
        ? g_Data.BootSector.TotalSectors16 : g_Data.BootSector.TotalSectors32;

    if (bytesPerSector != FAT_SECTOR_SIZE || !power_of_two(sectorsPerCluster) ||
        sectorsPerCluster > 128 || reservedSectors == 0 || fatCount == 0 ||
        sectorsPerFat == 0 || g_Data.BootSector.RootEntryCount == 0 ||
        totalSectors > disk->SectorCount) return false;

    const uint32_t rootBytes = (uint32_t)g_Data.BootSector.RootEntryCount * 32;
    g_RootDirectorySectors = (rootBytes + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    g_RootDirectoryLba = reservedSectors + fatCount * sectorsPerFat;
    g_DataSectionLba = g_RootDirectoryLba + g_RootDirectorySectors;
    if (g_DataSectionLba >= totalSectors) return false;

    g_ClusterCount = (totalSectors - g_DataSectionLba) / sectorsPerCluster;
    if (g_ClusterCount < 4085) g_FatType = 12;
    else if (g_ClusterCount < 65525) g_FatType = 16;
    else return false;

    g_FatSizeBytes = sectorsPerFat * FAT_SECTOR_SIZE;
    if (g_FatSizeBytes > sizeof(g_Fat)) return false;
    for (uint32_t sector = 0; sector < sectorsPerFat;) {
        const uint8_t count = (uint8_t)min_u32(sectorsPerFat - sector, 255);
        if (!DISK_ReadSectors(disk, reservedSectors + sector, count,
                              g_Fat + sector * FAT_SECTOR_SIZE)) return false;
        sector += count;
    }

    g_Data.RootDirectory.Public.Handle = FAT_ROOT_HANDLE;
    g_Data.RootDirectory.Public.IsDirectory = true;
    g_Data.RootDirectory.Public.Size = rootBytes;
    g_Data.RootDirectory.Opened = true;
    for (int i = 0; i < FAT_MAX_HANDLES; i++) {
        g_Data.OpenedFiles[i].Public.Handle = i;
    }
    g_Initialized = true;
    return true;
}

static bool make_short_name(const char* name, uint8_t output[11]) {
    for (int i = 0; i < 11; i++) output[i] = ' ';

    static const char kernelMemoryName[] = "KERNEL_MEMORY";
    static const char kernelMemoryAlias[] = "KERNELMEMORY";
    uint32_t nameLength = string_length(name);
    if (nameLength == sizeof(kernelMemoryName) - 1 ||
        nameLength == sizeof(kernelMemoryAlias) - 1) {
        bool isKernelMemory = true;
        uint32_t aliasIndex = 0;
        for (uint32_t i = 0; i < nameLength; i++) {
            if (nameLength == sizeof(kernelMemoryName) - 1 && name[i] == '_') continue;
            char character = name[i];
            if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
            if (aliasIndex >= sizeof(kernelMemoryAlias) - 1 ||
                character != kernelMemoryAlias[aliasIndex++]) {
                isKernelMemory = false;
                break;
            }
        }
        if (isKernelMemory) {
            const char shortName[] = "KERNEL_M";
            for (uint32_t i = 0; i < sizeof(shortName) - 1; i++) output[i] = shortName[i];
            return true;
        }
    }

    uint32_t baseLength = 0;
    uint32_t extensionLength = 0;
    bool inExtension = false;
    for (const char* current = name; *current; current++) {
        if (*current == '.') {
            if (inExtension) return false;
            inExtension = true;
            continue;
        }
        char character = *current;
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if (character < 0x21 || character > 0x7E || character == '/' || character == '\\') {
            return false;
        }
        if (inExtension) {
            if (extensionLength >= 3) return false;
            output[8 + extensionLength++] = (uint8_t)character;
        } else {
            if (baseLength >= 8) return false;
            output[baseLength++] = (uint8_t)character;
        }
    }
    return baseLength > 0;
}

static FAT_FileData* file_data_for(FAT_File* file) {
    if (file->Handle == FAT_ROOT_HANDLE) return &g_Data.RootDirectory;
    if (file->Handle >= 0 && file->Handle < FAT_MAX_HANDLES &&
        g_Data.OpenedFiles[file->Handle].Opened) {
        return &g_Data.OpenedFiles[file->Handle];
    }
    return NULL;
}

static bool find_entry(DISK* disk, FAT_File* directory, const char* name,
                       FAT_DirectoryEntry* result, uint32_t* entryLba,
                       uint16_t* entryOffset) {
    uint8_t shortName[11];
    if (!make_short_name(name, shortName)) return false;

    FAT_FileData* directoryData = file_data_for(directory);
    if (directoryData == NULL) return false;
    directory->Position = 0;
    FAT_DirectoryEntry entry;
    while (FAT_ReadEntry(disk, directory, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID)) continue;
        if (equal_bytes(shortName, entry.Name, sizeof(shortName))) {
            *result = entry;
            *entryLba = directoryData->BufferLba;
            *entryOffset = (uint16_t)((directory->Position - sizeof(entry)) % FAT_SECTOR_SIZE);
            directory->Position = 0;
            return true;
        }
    }
    directory->Position = 0;
    return false;
}

static FAT_File* open_entry(const FAT_DirectoryEntry* entry, uint32_t entryLba,
                            uint16_t entryOffset) {
    int handle = -1;
    for (int i = 0; i < FAT_MAX_HANDLES; i++) {
        if (!g_Data.OpenedFiles[i].Opened) {
            handle = i;
            break;
        }
    }
    if (handle < 0) return NULL;

    FAT_FileData* file = &g_Data.OpenedFiles[handle];
    clear_bytes(file, sizeof(*file));
    file->Public.Handle = handle;
    file->Public.IsDirectory = (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
    file->Public.Size = file->Public.IsDirectory ? 0xFFFFFFFFu : entry->Size;
    file->FirstCluster = (uint32_t)entry->FirstClusterLow |
                         ((uint32_t)entry->FirstClusterHigh << 16);
    if (g_FatType == 12) file->FirstCluster &= 0x0FFFu;
    file->CurrentCluster = file->FirstCluster;
    file->DirectoryEntryLba = entryLba;
    file->DirectoryEntryOffset = entryOffset;
    file->Opened = true;

    if ((entry->Size != 0 || file->Public.IsDirectory) &&
        !cluster_valid(file->FirstCluster)) {
        file->Opened = false;
        return NULL;
    }
    return &file->Public;
}

uint32_t FAT_Read(DISK* disk, FAT_File* publicFile, uint32_t byteCount, void* dataOut) {
    if (!g_Initialized || disk == NULL || publicFile == NULL || dataOut == NULL ||
        byteCount == 0) return 0;

    FAT_FileData* file;
    if (publicFile->Handle == FAT_ROOT_HANDLE) {
        file = &g_Data.RootDirectory;
    } else if (publicFile->Handle >= 0 && publicFile->Handle < FAT_MAX_HANDLES &&
               g_Data.OpenedFiles[publicFile->Handle].Opened) {
        file = &g_Data.OpenedFiles[publicFile->Handle];
    } else {
        return 0;
    }

    if (!publicFile->IsDirectory) {
        if (publicFile->Position >= publicFile->Size) return 0;
        byteCount = min_u32(byteCount, publicFile->Size - publicFile->Position);
    } else if (publicFile->Handle == FAT_ROOT_HANDLE) {
        if (publicFile->Position >= publicFile->Size) return 0;
        byteCount = min_u32(byteCount, publicFile->Size - publicFile->Position);
    }

    uint8_t* output = (uint8_t*)dataOut;
    uint32_t totalRead = 0;
    while (byteCount > 0) {
        const uint32_t sectorIndex = publicFile->Position / FAT_SECTOR_SIZE;
        const uint32_t sectorOffset = publicFile->Position % FAT_SECTOR_SIZE;
        if (!file->BufferValid || file->BufferSectorIndex != sectorIndex) {
            if (!read_file_sector(disk, file, sectorIndex)) break;
        }
        const uint32_t count = min_u32(byteCount, FAT_SECTOR_SIZE - sectorOffset);
        copy_bytes(output, file->Buffer + sectorOffset, count);
        output += count;
        publicFile->Position += count;
        totalRead += count;
        byteCount -= count;
    }
    return totalRead;
}

bool FAT_ReadEntry(DISK* disk, FAT_File* file, FAT_DirectoryEntry* dirEntry) {
    if (dirEntry == NULL) return false;
    return FAT_Read(disk, file, sizeof(*dirEntry), dirEntry) == sizeof(*dirEntry);
}

bool FAT_Save(DISK* disk, FAT_File* publicFile, const void* data, uint32_t byteCount) {
    if (!g_Initialized || disk == NULL || publicFile == NULL || data == NULL ||
        publicFile->Handle < 0 || publicFile->Handle >= FAT_MAX_HANDLES ||
        publicFile->IsDirectory || !g_Data.OpenedFiles[publicFile->Handle].Opened) {
        return false;
    }

    FAT_FileData* file = &g_Data.OpenedFiles[publicFile->Handle];
    const uint32_t clusterBytes =
        FAT_SECTOR_SIZE * g_Data.BootSector.SectorsPerCluster;
    uint32_t allocatedBytes = 0;
    uint32_t cluster = file->FirstCluster;
    for (uint32_t traversed = 0; traversed < g_ClusterCount; traversed++) {
        if (!cluster_valid(cluster)) return false;
        allocatedBytes += clusterBytes;
        const uint32_t next = next_cluster(cluster);
        if (is_end_cluster(next)) break;
        if (!cluster_valid(next) || next == FAT_BAD_CLUSTER) return false;
        cluster = next;
    }
    if (byteCount > allocatedBytes) return false;

    const uint8_t* input = (const uint8_t*)data;
    const uint32_t sectorCount = (byteCount + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    for (uint32_t sectorIndex = 0; sectorIndex < sectorCount; sectorIndex++) {
        file->BufferValid = false;
        if (!read_file_sector(disk, file, sectorIndex)) return false;
        clear_bytes(file->Buffer, FAT_SECTOR_SIZE);
        const uint32_t offset = sectorIndex * FAT_SECTOR_SIZE;
        const uint32_t remaining = byteCount - offset;
        const uint32_t count = min_u32(remaining, FAT_SECTOR_SIZE);
        copy_bytes(file->Buffer, input + offset, count);
        if (!DISK_WriteSectors(disk, file->BufferLba, 1, file->Buffer)) return false;
    }

    uint8_t directorySector[FAT_SECTOR_SIZE];
    if (file->DirectoryEntryOffset + sizeof(FAT_DirectoryEntry) > FAT_SECTOR_SIZE ||
        !DISK_ReadSectors(disk, file->DirectoryEntryLba, 1, directorySector)) return false;
    uint8_t* sizeField = directorySector + file->DirectoryEntryOffset + 28;
    sizeField[0] = (uint8_t)byteCount;
    sizeField[1] = (uint8_t)(byteCount >> 8);
    sizeField[2] = (uint8_t)(byteCount >> 16);
    sizeField[3] = (uint8_t)(byteCount >> 24);
    if (!DISK_WriteSectors(disk, file->DirectoryEntryLba, 1, directorySector)) return false;

    file->Public.Size = byteCount;
    file->Public.Position = 0;
    file->BufferValid = false;
    return true;
}

static void set_fat_entry(uint32_t cluster, uint32_t value) {
    if (g_FatType == 12) {
        const uint32_t offset = cluster + cluster / 2;
        uint16_t current = read_u16(g_Fat + offset);
        if (cluster & 1) current = (uint16_t)((current & 0x000F) | (value << 4));
        else current = (uint16_t)((current & 0xF000) | (value & 0x0FFF));
        g_Fat[offset] = (uint8_t)current;
        g_Fat[offset + 1] = (uint8_t)(current >> 8);
    } else {
        const uint32_t offset = cluster * 2;
        g_Fat[offset] = (uint8_t)value;
        g_Fat[offset + 1] = (uint8_t)(value >> 8);
    }
}

static bool write_fat_copies(DISK* disk) {
    const uint32_t sectorsPerFat = g_Data.BootSector.SectorsPerFat;
    for (uint32_t copy = 0; copy < g_Data.BootSector.FatCount; copy++) {
        uint32_t sector = 0;
        while (sector < sectorsPerFat) {
            const uint8_t count = (uint8_t)min_u32(sectorsPerFat - sector, 255);
            const uint32_t lba = g_Data.BootSector.ReservedSectors +
                                 copy * sectorsPerFat + sector;
            if (!DISK_WriteSectors(disk, lba, count,
                                   g_Fat + sector * FAT_SECTOR_SIZE)) return false;
            sector += count;
        }
    }
    return true;
}

static bool allocate_file_clusters(DISK* disk, uint32_t byteCapacity,
                                   uint32_t* firstCluster) {
    const uint32_t clusterBytes =
        FAT_SECTOR_SIZE * g_Data.BootSector.SectorsPerCluster;
    uint32_t required = (byteCapacity + clusterBytes - 1) / clusterBytes;
    if (required == 0) required = 1;
    if (required > 4) return false;

    uint32_t clusters[4];
    uint32_t found = 0;
    for (uint32_t candidate = 2; candidate < g_ClusterCount + 2 && found < required;
         candidate++) {
        if (next_cluster(candidate) == 0) clusters[found++] = candidate;
    }
    if (found != required) return false;

    for (uint32_t i = 0; i < required; i++) {
        const uint32_t endMarker = g_FatType == 12 ? 0x0FFFu : 0xFFFFu;
        set_fat_entry(clusters[i], i + 1 < required ? clusters[i + 1] : endMarker);
    }
    if (!write_fat_copies(disk)) return false;

    uint8_t zeroSector[FAT_SECTOR_SIZE];
    clear_bytes(zeroSector, sizeof(zeroSector));
    for (uint32_t i = 0; i < required; i++) {
        const uint32_t firstLba = cluster_to_lba(clusters[i]);
        for (uint32_t sector = 0; sector < g_Data.BootSector.SectorsPerCluster; sector++) {
            if (!DISK_WriteSectors(disk, firstLba + sector, 1, zeroSector)) return false;
        }
    }
    *firstCluster = clusters[0];
    return true;
}

FAT_File* FAT_Create(DISK* disk, const char* path) {
    if (!g_Initialized || disk == NULL || path == NULL || *path == '\0') return NULL;

    const char* lastSlash = NULL;
    for (const char* current = path; *current; current++) {
        if (*current == '/') lastSlash = current;
    }
    const char* fileName = lastSlash != NULL ? lastSlash + 1 : path;
    if (*fileName == '\0') return NULL;

    char directoryPath[256];
    if (lastSlash == NULL || lastSlash == path) {
        directoryPath[0] = '/';
        directoryPath[1] = '\0';
    } else {
        const uint32_t directoryLength = (uint32_t)(lastSlash - path);
        if (directoryLength >= sizeof(directoryPath)) return NULL;
        copy_bytes(directoryPath, path, directoryLength);
        directoryPath[directoryLength] = '\0';
    }

    uint8_t shortName[11];
    if (!make_short_name(fileName, shortName)) return NULL;
    FAT_File* directory = FAT_Open(disk, directoryPath);
    if (directory == NULL || !directory->IsDirectory) {
        if (directory != NULL) FAT_Close(directory);
        return NULL;
    }

    FAT_FileData* directoryData = file_data_for(directory);
    uint32_t directoryEntryLba = 0;
    uint16_t directoryEntryOffset = 0;
    bool foundSlot = false;
    FAT_DirectoryEntry existingEntry;
    while (FAT_ReadEntry(disk, directory, &existingEntry)) {
        const uint32_t entryOffset =
            (directory->Position - sizeof(existingEntry)) % FAT_SECTOR_SIZE;
        if (existingEntry.Name[0] == 0 || existingEntry.Name[0] == 0xE5) {
            directoryEntryLba = directoryData->BufferLba;
            directoryEntryOffset = (uint16_t)entryOffset;
            foundSlot = true;
            break;
        }
        if (equal_bytes(shortName, existingEntry.Name, sizeof(shortName))) {
            FAT_Close(directory);
            return NULL;
        }
    }
    if (!foundSlot) {
        FAT_Close(directory);
        return NULL;
    }

    uint32_t firstCluster;
    if (!allocate_file_clusters(disk, 2048, &firstCluster)) {
        FAT_Close(directory);
        return NULL;
    }

    uint8_t entryBytes[sizeof(FAT_DirectoryEntry)];
    clear_bytes(entryBytes, sizeof(entryBytes));
    copy_bytes(entryBytes, shortName, sizeof(shortName));
    entryBytes[11] = FAT_ATTRIBUTE_ARCHIVE;
    entryBytes[26] = (uint8_t)firstCluster;
    entryBytes[27] = (uint8_t)(firstCluster >> 8);
    if (!DISK_ReadSectors(disk, directoryEntryLba, 1, directoryData->Buffer)) {
        FAT_Close(directory);
        return NULL;
    }
    copy_bytes(directoryData->Buffer + directoryEntryOffset, entryBytes, sizeof(entryBytes));
    if (!DISK_WriteSectors(disk, directoryEntryLba, 1, directoryData->Buffer)) {
        FAT_Close(directory);
        return NULL;
    }

    FAT_DirectoryEntry newEntry;
    copy_bytes(&newEntry, entryBytes, sizeof(newEntry));
    FAT_Close(directory);
    return open_entry(&newEntry, directoryEntryLba, directoryEntryOffset);
}
bool FAT_Mkdir(DISK* disk, const char* path) {
    if (!g_Initialized || disk == NULL || path == NULL || *path == '\0') return false;

    const char* lastSlash = NULL;
    for (const char* current = path; *current; current++) {
        if (*current == '/') lastSlash = current;
    }
    const char* dirName = lastSlash != NULL ? lastSlash + 1 : path;
    if (*dirName == '\0') return false;

    char parentPath[256];
    if (lastSlash == NULL || lastSlash == path) {
        parentPath[0] = '/';
        parentPath[1] = '\0';
    } else {
        const uint32_t parentLength = (uint32_t)(lastSlash - path);
        if (parentLength >= sizeof(parentPath)) return false;
        copy_bytes(parentPath, path, parentLength);
        parentPath[parentLength] = '\0';
    }

    uint8_t shortName[11];
    if (!make_short_name(dirName, shortName)) return false;

    FAT_File* parent = FAT_Open(disk, parentPath);
    if (parent == NULL || !parent->IsDirectory) {
        if (parent != NULL) FAT_Close(parent);
        return false;
    }

    FAT_FileData* parentData = file_data_for(parent);
    const uint32_t parentCluster = parentData->FirstCluster;

    uint32_t slotLba = 0;
    uint16_t slotOffset = 0;
    bool foundSlot = false;
    FAT_DirectoryEntry existing;
    while (FAT_ReadEntry(disk, parent, &existing)) {
        const uint32_t entryOffset =
            (parent->Position - sizeof(existing)) % FAT_SECTOR_SIZE;
        if (existing.Name[0] == 0 || existing.Name[0] == 0xE5) {
            slotLba = parentData->BufferLba;
            slotOffset = (uint16_t)entryOffset;
            foundSlot = true;
            break;
        }
        if (equal_bytes(shortName, existing.Name, sizeof(shortName))) {
            FAT_Close(parent);
            return false;
        }
    }
    if (!foundSlot) {
        FAT_Close(parent);
        return false;
    }

    uint32_t firstCluster;
    const uint32_t clusterBytes = FAT_SECTOR_SIZE * g_Data.BootSector.SectorsPerCluster;
    if (!allocate_file_clusters(disk, clusterBytes, &firstCluster)) {
        FAT_Close(parent);
        return false;
    }

    uint8_t sector[FAT_SECTOR_SIZE];
    clear_bytes(sector, sizeof(sector));
    for (int i = 0; i < 11; i++) {
        sector[i] = ' ';
        sector[32 + i] = ' ';
    }
    sector[0] = '.';
    sector[32] = '.';
    sector[33] = '.';
    sector[11] = FAT_ATTRIBUTE_DIRECTORY;
    sector[32 + 11] = FAT_ATTRIBUTE_DIRECTORY;
    sector[26] = (uint8_t)firstCluster;
    sector[27] = (uint8_t)(firstCluster >> 8);
    sector[32 + 26] = (uint8_t)parentCluster;
    sector[32 + 27] = (uint8_t)(parentCluster >> 8);
    if (!DISK_WriteSectors(disk, cluster_to_lba(firstCluster), 1, sector)) {
        FAT_Close(parent);
        return false;
    }

    uint8_t entryBytes[sizeof(FAT_DirectoryEntry)];
    clear_bytes(entryBytes, sizeof(entryBytes));
    copy_bytes(entryBytes, shortName, sizeof(shortName));
    entryBytes[11] = FAT_ATTRIBUTE_DIRECTORY;
    entryBytes[26] = (uint8_t)firstCluster;
    entryBytes[27] = (uint8_t)(firstCluster >> 8);
    if (!DISK_ReadSectors(disk, slotLba, 1, parentData->Buffer)) {
        FAT_Close(parent);
        return false;
    }
    copy_bytes(parentData->Buffer + slotOffset, entryBytes, sizeof(entryBytes));
    const bool ok = DISK_WriteSectors(disk, slotLba, 1, parentData->Buffer);
    FAT_Close(parent);
    return ok;
}
FAT_File* FAT_Open(DISK* disk, const char* path) {
    if (!g_Initialized || disk == NULL || path == NULL) return NULL;
    while (*path == '/') path++;

    FAT_File* current = &g_Data.RootDirectory.Public;
    if (*path == '\0') {
        current->Position = 0;
        return current;
    }

    while (*path) {
        char component[13];
        uint32_t length = 0;
        while (*path && *path != '/') {
            if (length >= sizeof(component) - 1) {
                FAT_Close(current);
                return NULL;
            }
            component[length++] = *path++;
        }
        component[length] = '\0';
        while (*path == '/') path++;
        const bool isLast = *path == '\0';

        FAT_DirectoryEntry entry;
        uint32_t entryLba;
        uint16_t entryOffset;
        if (!current->IsDirectory ||
            !find_entry(disk, current, component, &entry, &entryLba, &entryOffset)) {
            FAT_Close(current);
            return NULL;
        }
        if (!isLast && (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) == 0) {
            FAT_Close(current);
            return NULL;
        }

        if (current->Handle != FAT_ROOT_HANDLE) FAT_Close(current);
        current = open_entry(&entry, entryLba, entryOffset);
        if (current == NULL) return NULL;
    }
    return current;
}

static bool is_dot_entry(const uint8_t name[11]) {
    if (name[0] != '.') return false;
    if (name[1] == '.' || name[1] == ' ') {
        for (int i = name[1] == '.' ? 2 : 1; i < 11; i++) {
            if (name[i] != ' ') return false;
        }
        return true;
    }
    return false;
}

static bool free_cluster_chain(DISK* disk, uint32_t firstCluster) {
    if (firstCluster < 2) return true;

    uint32_t cluster = firstCluster;
    uint32_t traversed = 0;
    bool reachedEnd = false;
    while (traversed < g_ClusterCount) {
        if (!cluster_valid(cluster)) return false;
        const uint32_t next = next_cluster(cluster);
        if (next == FAT_BAD_CLUSTER || (!is_end_cluster(next) && !cluster_valid(next))) {
            return false;
        }
        traversed++;
        if (is_end_cluster(next)) {
            reachedEnd = true;
            break;
        }
        cluster = next;
    }
    if (traversed == 0 || !reachedEnd) return false;

    cluster = firstCluster;
    for (uint32_t i = 0; i < traversed; i++) {
        const uint32_t next = next_cluster(cluster);
        set_fat_entry(cluster, 0);
        if (is_end_cluster(next)) break;
        cluster = next;
    }
    return write_fat_copies(disk);
}

static bool mark_entry_deleted(DISK* disk, uint32_t lba, uint16_t offset) {
    uint8_t sector[FAT_SECTOR_SIZE];
    if (offset + sizeof(FAT_DirectoryEntry) > FAT_SECTOR_SIZE ||
        !DISK_ReadSectors(disk, lba, 1, sector)) return false;
    sector[offset] = 0xE5;
    return DISK_WriteSectors(disk, lba, 1, sector);
}

static bool remove_directory_contents(DISK* disk, FAT_File* directory, uint32_t depth) {
    if (depth > 8) return false;
    FAT_FileData* directoryData = file_data_for(directory);
    if (directoryData == NULL) return false;
    directory->Position = 0;

    FAT_DirectoryEntry entry;
    while (1) {
        const uint32_t entryPosition = directory->Position;
        if (!FAT_ReadEntry(disk, directory, &entry)) break;
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID) || is_dot_entry(entry.Name)) continue;

        const uint32_t entryLba = directoryData->BufferLba;
        const uint16_t entryOffset = (uint16_t)(entryPosition % FAT_SECTOR_SIZE);
        const uint32_t firstCluster = (uint32_t)entry.FirstClusterLow |
                                      ((uint32_t)entry.FirstClusterHigh << 16);
        if ((entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0) {
            FAT_File* child = open_entry(&entry, entryLba, entryOffset);
            if (child == NULL || !remove_directory_contents(disk, child, depth + 1)) {
                if (child != NULL) FAT_Close(child);
                return false;
            }
            FAT_Close(child);
        }
        if (!free_cluster_chain(disk, firstCluster) ||
            !mark_entry_deleted(disk, entryLba, entryOffset)) return false;
    }
    directory->Position = 0;
    return true;
}

static bool directory_is_empty(DISK* disk, FAT_File* directory) {
    directory->Position = 0;
    FAT_DirectoryEntry entry;
    while (FAT_ReadEntry(disk, directory, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID) || is_dot_entry(entry.Name)) continue;
        directory->Position = 0;
        return false;
    }
    directory->Position = 0;
    return true;
}

bool FAT_Remove(DISK* disk, const char* path, bool recursive) {
    if (!g_Initialized || disk == NULL || path == NULL || *path == '\0') return false;

    const char* lastSlash = NULL;
    for (const char* current = path; *current; current++) {
        if (*current == '/') lastSlash = current;
    }
    const char* name = lastSlash != NULL ? lastSlash + 1 : path;
    if (*name == '\0') return false;

    char parentPath[256];
    if (lastSlash == NULL || lastSlash == path) {
        parentPath[0] = '/';
        parentPath[1] = '\0';
    } else {
        const uint32_t parentLength = (uint32_t)(lastSlash - path);
        if (parentLength >= sizeof(parentPath)) return false;
        copy_bytes(parentPath, path, parentLength);
        parentPath[parentLength] = '\0';
    }

    FAT_File* parent = FAT_Open(disk, parentPath);
    if (parent == NULL || !parent->IsDirectory) {
        if (parent != NULL) FAT_Close(parent);
        return false;
    }

    FAT_DirectoryEntry entry;
    uint32_t entryLba;
    uint16_t entryOffset;
    if (!find_entry(disk, parent, name, &entry, &entryLba, &entryOffset)) {
        FAT_Close(parent);
        return false;
    }

    const uint32_t firstCluster = (uint32_t)entry.FirstClusterLow |
                                  ((uint32_t)entry.FirstClusterHigh << 16);
    if ((entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0) {
        FAT_File* directory = open_entry(&entry, entryLba, entryOffset);
        if (directory == NULL) {
            FAT_Close(parent);
            return false;
        }
        const bool removedContents = recursive
            ? remove_directory_contents(disk, directory, 1)
            : directory_is_empty(disk, directory);
        FAT_Close(directory);
        if (!removedContents) {
            FAT_Close(parent);
            return false;
        }
    }

    if (!free_cluster_chain(disk, firstCluster)) {
        FAT_Close(parent);
        return false;
    }
    if (!mark_entry_deleted(disk, entryLba, entryOffset)) {
        FAT_Close(parent);
        return false;
    }
    FAT_Close(parent);
    return true;
}

void FAT_Close(FAT_File* file) {
    if (file == NULL || !g_Initialized) return;
    if (file->Handle == FAT_ROOT_HANDLE) {
        file->Position = 0;
        g_Data.RootDirectory.BufferValid = false;
    } else if (file->Handle >= 0 && file->Handle < FAT_MAX_HANDLES) {
        g_Data.OpenedFiles[file->Handle].Opened = false;
    }
}