#ifndef NEUTRA_SYSTEM_H
#define NEUTRA_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include "ata.h"
#include "dce.h"
#include "desktop.h"
#include "disk.h"
#include "explorer.h"
#include "fat.h"
#include "graphics.h"
#include "idt.h"
#include "kernel.h"
#include "scheduler.h"
#include "shell.h"
#include "vga.h"
#include "window.h"

void set_color(uint32_t color);uint32_t get_color(void);
void putpixel(int x, int y);
void line(int x0, int y0, int x1, int y1);
void rect(int x, int y, int width, int height);
void clear_screen(uint32_t color);
void present(void);
int display_bmp(const void* bmp_data, uint32_t bmp_length,int x, int y, int width, int height, int size);

#endif