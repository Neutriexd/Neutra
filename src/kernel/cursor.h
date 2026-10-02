#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

void cursor_init(void);
void vga_mouse_update(int delta_x, int delta_y, uint8_t buttons);

#endif