#include "kernel_memory.h"

#define KM_MAX_FILES 32
#define KM_NAME_CAPACITY 128

typedef struct {
    uint32_t type;
    uint32_t size;
} KM_TagHeader;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t start;
    uint32_t end;
    char name[];
} KM_ModuleTag;

typedef struct {
    const uint8_t* data;
    uint32_t size;
    char name[KM_NAME_CAPACITY];
} KM_File;

static KM_File km_files[KM_MAX_FILES];
static int km_file_count;

static int km_string_equal(const char* left, const char* right) {
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void km_copy(char* destination, const char* source, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) destination[i] = source[i];
    destination[length] = '\0';
}

void kernel_memory_init(uint32_t multiboot_info_address) {
    km_file_count = 0;
    if (multiboot_info_address == 0) return;

    const uint32_t total_size = *(const uint32_t*)(uintptr_t)multiboot_info_address;
    if (total_size < 16) return;

    uint32_t offset = 8;
    while (offset <= total_size - sizeof(KM_TagHeader)) {
        const KM_TagHeader* header =
            (const KM_TagHeader*)(uintptr_t)(multiboot_info_address + offset);
        if (header->size < sizeof(KM_TagHeader) || header->size > total_size - offset) return;
        if (header->type == 0) return;

        if (header->type == 3 && header->size >= sizeof(KM_ModuleTag)) {
            const KM_ModuleTag* module =
                (const KM_ModuleTag*)(uintptr_t)(multiboot_info_address + offset);
            const uint32_t name_capacity = header->size - sizeof(KM_ModuleTag);
            uint32_t name_length = 0;
            while (name_length < name_capacity && module->name[name_length] != '\0') name_length++;
            if (name_length < name_capacity && module->end >= module->start &&
                module->name[0] != '\0' && km_file_count < KM_MAX_FILES &&
                name_length < KM_NAME_CAPACITY) {
                km_copy(km_files[km_file_count].name, module->name, name_length);
                km_files[km_file_count].data = (const uint8_t*)(uintptr_t)module->start;
                km_files[km_file_count].size = module->end - module->start;
                km_file_count++;
            }
        }

        const uint32_t next = offset + ((header->size + 7) & ~7u);
        if (next <= offset || next > total_size) return;
        offset = next;
    }
}

int kernel_memory_get_file(const char* path, const uint8_t** data, uint32_t* size) {
    if (path == 0 || data == 0 || size == 0) return 0;
    while (*path == '/') path++;
    if (path[0] == '\0') return 0;

    const char* relative_path = path;
    const char project_prefix[] = "src/kernel/";
    uint32_t prefix_length = sizeof(project_prefix) - 1;
    uint32_t i = 0;
    while (i < prefix_length && relative_path[i] == project_prefix[i]) i++;
    if (i == prefix_length) relative_path += prefix_length;

    const char kernel_memory_prefix[] = "kernel_memory/";
    prefix_length = sizeof(kernel_memory_prefix) - 1;
    i = 0;
    while (i < prefix_length && relative_path[i] == kernel_memory_prefix[i]) i++;
    if (i == prefix_length) relative_path += prefix_length;

    for (int file_index = 0; file_index < km_file_count; file_index++) {
        const char* module_path = km_files[file_index].name;
        const char* module_relative_path = module_path;
        i = 0;
        while (i < prefix_length && module_path[i] == kernel_memory_prefix[i]) i++;
        if (i == prefix_length) module_relative_path += prefix_length;
        if (km_string_equal(path, module_path) ||
            km_string_equal(relative_path, module_relative_path)) {
            *data = km_files[file_index].data;
            *size = km_files[file_index].size;
            return 1;
        }
    }
    return 0;
}