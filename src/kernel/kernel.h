#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include "scheduler.h"
#include "vga.h"

void int_to_str(int num, char* buf, int buf_size);
void hex_to_str(unsigned int num, char* buf, int buf_size);
void memset(void* ptr, int value, size_t num);
void memcpy(void* dest, const void* src, size_t num);
int strcmp(const char* str1, const char* str2);

void execute_cde_task(void);
void run_program(void);
void test_framebuffer(void);
void kernel_entry(uint32_t mbt_addr);

#endif