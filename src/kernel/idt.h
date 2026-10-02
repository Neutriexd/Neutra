#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

void idt_init(void);
void keyboard_handler_install(void);
char keyboard_getchar_poll(void);
char keyboard_try_getchar_poll(void);

int mouse_hitbox_check(int x_min, int y_min, int x_max, int y_max);
void mouse_get_position(int* out_x, int* out_y);
int mouse_get_buttons(void);
void mouse_set_position(int x, int y);

#endif