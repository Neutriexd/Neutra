#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#define WIN_EDGE 6

#define WIN_MODE_NONE 0
#define WIN_MODE_MOVE 1
#define WIN_MODE_RESIZE 2

typedef struct {
    int x, y, w, h;
    int min_w, min_h, max_w, max_h;
    int title_h;
    int close_w;
    int mode;
    int resize_right, resize_bottom;
    int grab_dx, grab_dy;
    int start_w, start_h, start_mx, start_my;
    int prev_buttons;
} Window;

void window_init(Window* win, int x, int y, int w, int h,
                 int min_w, int min_h, int max_w, int max_h,
                 int title_h, int close_w);

int window_update(Window* win, int mouse_x, int mouse_y, int buttons);

void vga_redraw_region(int x, int y, int w, int h);
int vga_set_input_captured(int captured);

#endif