#include "window.h"
#include "graphics.h"

void window_init(Window* win, int x, int y, int w, int h,
                 int min_w, int min_h, int max_w, int max_h,
                 int title_h, int close_w) {
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->min_w = min_w;
    win->min_h = min_h;
    win->max_w = max_w;
    win->max_h = max_h;
    win->title_h = title_h;
    win->close_w = close_w;
    win->mode = WIN_MODE_NONE;
    win->resize_right = 0;
    win->resize_bottom = 0;
    win->grab_dx = 0;
    win->grab_dy = 0;
    win->start_w = w;
    win->start_h = h;
    win->start_mx = 0;
    win->start_my = 0;
    win->prev_buttons = 0;
}

static int clamp_int(int value, int low, int high) {
    if (high < low) high = low;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

int window_update(Window* win, int mouse_x, int mouse_y, int buttons) {
    const int screen_w = (int)fb_info.framebuffer_width;
    const int screen_h = (int)fb_info.framebuffer_height;
    const int left = (buttons & 1) != 0;
    const int pressed = left && !(win->prev_buttons & 1);
    const int old_x = win->x, old_y = win->y, old_w = win->w, old_h = win->h;

    win->prev_buttons = buttons;
    if (!left) {
        win->mode = WIN_MODE_NONE;
        return 0;
    }

    if (pressed) {
        win->mode = WIN_MODE_NONE;
        if (mouse_x >= win->x && mouse_x < win->x + win->w &&
            mouse_y >= win->y && mouse_y < win->y + win->h) {
            const int on_right = mouse_x >= win->x + win->w - WIN_EDGE;
            const int on_bottom = mouse_y >= win->y + win->h - WIN_EDGE;
            if (on_right || on_bottom) {
                win->mode = WIN_MODE_RESIZE;
                win->resize_right = on_right;
                win->resize_bottom = on_bottom;
                win->start_w = win->w;
                win->start_h = win->h;
                win->start_mx = mouse_x;
                win->start_my = mouse_y;
            } else if (mouse_y < win->y + win->title_h &&
                       mouse_x < win->x + win->w - win->close_w) {
                win->mode = WIN_MODE_MOVE;
                win->grab_dx = mouse_x - win->x;
                win->grab_dy = mouse_y - win->y;
            }
        }
    }

    if (win->mode == WIN_MODE_MOVE) {
        win->x = clamp_int(mouse_x - win->grab_dx, 0, screen_w - win->w);
        win->y = clamp_int(mouse_y - win->grab_dy, 0, screen_h - win->h);
    } else if (win->mode == WIN_MODE_RESIZE) {
        if (win->resize_right) {
            const int wanted = win->start_w + (mouse_x - win->start_mx);
            const int high = win->max_w < screen_w - win->x ? win->max_w : screen_w - win->x;
            win->w = clamp_int(wanted, win->min_w, high);
        }
        if (win->resize_bottom) {
            const int wanted = win->start_h + (mouse_y - win->start_my);
            const int high = win->max_h < screen_h - win->y ? win->max_h : screen_h - win->y;
            win->h = clamp_int(wanted, win->min_h, high);
        }
    }

    return win->x != old_x || win->y != old_y || win->w != old_w || win->h != old_h;
}