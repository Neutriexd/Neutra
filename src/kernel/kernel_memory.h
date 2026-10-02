#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>

void kernel_memory_init(uint32_t multiboot_info_address);
int kernel_memory_get_file(const char* path, const uint8_t** data, uint32_t* size);

#endif