
#include <stdint.h>
#include <stddef.h>

#include "kernel.h"
#include "vga.h"

#ifndef VGA_EV_CELL
#define VGA_EV_CELL   0   
#endif
#ifndef VGA_EV_SCROLL
#define VGA_EV_SCROLL 1   
#endif
#ifndef VGA_EV_CLEAR
#define VGA_EV_CLEAR  2   
#endif
typedef void (*vga_listener_t)(int event, int x, int y);
void vga_set_listener(vga_listener_t listener);
uint16_t vga_get_cell(int x, int y);

static volatile int vga_x = 0;
static volatile int vga_y = 0;
static uint16_t text_cells[VGA_WIDTH * VGA_HEIGHT];
static vga_listener_t vga_listener;

static void notify(int event, int x, int y) {
    if (vga_listener) vga_listener(event, x, y);
}

void vga_set_listener(vga_listener_t listener) {
    vga_listener = listener;
}

uint16_t vga_get_cell(int x, int y) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return 0;
    return text_cells[y * VGA_WIDTH + x];
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = 0x0000;
        text_cells[i] = 0x0000;
    }
    vga_x = 0;
    vga_y = 0;
    notify(VGA_EV_CLEAR, 0, 0);
}

void vga_scroll(void) {
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        text_cells[i] = text_cells[i + VGA_WIDTH];
        VGA_MEMORY[i] = text_cells[i];
    }
    for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = 0x0000;
        text_cells[i] = 0x0000;
    }
    vga_y = VGA_HEIGHT - 1;
    vga_x = 0;
    notify(VGA_EV_SCROLL, 0, 0);
}

void vga_putchar(char c, unsigned char color) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else {
        int index = vga_y * VGA_WIDTH + vga_x;

        if (index < VGA_WIDTH * VGA_HEIGHT) {
            text_cells[index] = ((uint16_t)color << 8) | (unsigned char)c;
            VGA_MEMORY[index] = text_cells[index];
            notify(VGA_EV_CELL, vga_x, vga_y);
            vga_x++;
        }
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= VGA_HEIGHT) {
        vga_scroll();
    }
}

void vga_write(const char* str, unsigned char color) {
    if (str == NULL) {
        return;
    }

    while (*str) {
        vga_putchar(*str++, color);
    }
}

void vga_print_hex(unsigned int num, unsigned char color) {
    const char* hex_chars = "0123456789abcdef";
    char buf[16];
    int i = 0;

    if (num == 0) {
        vga_putchar('0', color);
        return;
    }

    unsigned int temp = num;

    while (temp > 0) {
        buf[i++] = hex_chars[temp & 0xF];
        temp >>= 4;
    }

    for (int j = i - 1; j >= 0; j--) {
        vga_putchar(buf[j], color);
    }
}

void vga_print_int(int num, unsigned char color) {
    char buf[32];
    int_to_str(num, buf, sizeof(buf));
    vga_write(buf, color);
}