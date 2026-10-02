#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

#define VGA_MEMORY ((volatile unsigned short*)0xb8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#ifndef VGA_EV_CELL
#define VGA_EV_CELL   0   
#endif
#ifndef VGA_EV_SCROLL
#define VGA_EV_SCROLL 1   
#endif
#ifndef VGA_EV_CLEAR
#define VGA_EV_CLEAR  2   
#endif
#define MAKE_COLOR(bg, fg) ((bg << 4) | fg)

#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GRAY 7
#define COLOR_DARK_GRAY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_LIGHT_YELLOW 14
#define COLOR_WHITE 15

void vga_clear(void);
void vga_scroll(void);
void vga_putchar(char c, unsigned char color);
void vga_write(const char* str, unsigned char color);
void vga_print_hex(unsigned int num, unsigned char color);
void vga_print_int(int num, unsigned char color);
void vga_draw_text_at(int x, int y, const char* text, uint32_t color);
void vga_backspace(void);
void vga_mouse_update(int delta_x, int delta_y, uint8_t buttons);

void int_to_str(int num, char* buf, int buf_size);
void hex_to_str(unsigned int num, char* buf, int buf_size);
void memset(void* ptr, int value, size_t num);
void memcpy(void* dest, const void* src, size_t num);
int strcmp(const char* str1, const char* str2);

#endif