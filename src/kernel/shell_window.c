
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
#include "graphics.h"
#include "font.h"
#include "shell_window.h"

void gfx_present(void);

#define FALLBACK_BACKGROUND 0x0878B0
#define TASKBAR_HEIGHT 36
#define FB_CELL_WIDTH 10
#define FB_CELL_HEIGHT 16
#define WINDOW_TITLE_HEIGHT 36
#define WINDOW_MAX_WIDTH 900
#define WINDOW_MAX_HEIGHT 540
#define CLOSE_SIZE 18

static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static int placed;
static int window_open;
static int window_x;
static int window_y;
static int dragging;
static int drag_dx;
static int drag_dy;
static int prev_buttons;
static void (*background_painter)(int x, int y, int w, int h);



static int window_width(void) {
    int width = (int)fb_info.framebuffer_width - 24;
    return width < WINDOW_MAX_WIDTH ? width : WINDOW_MAX_WIDTH;
}

static int window_height(void) {
    int height = (int)fb_info.framebuffer_height - 24 - TASKBAR_HEIGHT;
    return height < WINDOW_MAX_HEIGHT ? height : WINDOW_MAX_HEIGHT;
}

static void place_window(void) {
    if (placed || !fb_info.has_framebuffer) return;
    window_x = ((int)fb_info.framebuffer_width - window_width()) / 2;
    window_y = ((int)fb_info.framebuffer_height - TASKBAR_HEIGHT - window_height()) / 2;
    if (window_y < 0) window_y = 0;
    placed = 1;
}

static int in_close_button(int x, int y) {
    const int bx = window_x + window_width() - CLOSE_SIZE - 8;
    const int by = window_y + 4;
    return x >= bx && x < bx + CLOSE_SIZE && y >= by && y < by + CLOSE_SIZE;
}



static void paint_background(int x, int y, int w, int h) {
    if (background_painter) background_painter(x, y, w, h);
    else draw_rect(x, y, w, h, FALLBACK_BACKGROUND);
}

static void draw_chrome(const char* name) {
    const int width = window_width();
    const int height = window_height();
    draw_rect(window_x, window_y, width, height, 0x202020);
    draw_rect(window_x + 1, window_y + 1, width - 2, 25, 0x0078D4);
    draw_rect(window_x + 1, window_y + 26, width - 2, height - 27, 0x000000);
    vga_draw_text_at(window_x + 10, window_y + 7, name, 0xF0F4F8);

    const int bx = window_x + width - CLOSE_SIZE - 8;
    draw_rect(bx, window_y + 4, CLOSE_SIZE, CLOSE_SIZE, 0xD04040);
    vga_draw_text_at(bx + 4, window_y + 6, "X", 0xFFFFFF);
}

static void render_cell(int x, int y) {
    const int origin_x = window_x + 16 + x * FB_CELL_WIDTH;
    const int origin_y = window_y + 50 + y * FB_CELL_HEIGHT;
    if (origin_x + FB_CELL_WIDTH > window_x + window_width() - 12 ||
        origin_y + FB_CELL_HEIGHT > window_y + window_height() - 12) return;

    const uint16_t cell = vga_get_cell(x, y);
    const unsigned char c = (unsigned char)(cell & 0xFF);
    const unsigned char attributes = (unsigned char)(cell >> 8);
    const uint32_t foreground = palette[attributes & 0x0F];
    const uint32_t background = palette[(attributes >> 4) & 0x0F];
    draw_rect(origin_x, origin_y, FB_CELL_WIDTH, FB_CELL_HEIGHT, background);

    const uint8_t* glyph = font_glyph(c);
    if (glyph == 0) return;
    for (int row = 0; row < 7; row++) {
        for (int column = 0; column < 5; column++) {
            if (glyph[row] & (1u << (4 - column))) {
                draw_rect(origin_x + column * 2, origin_y + row * 2 + 1, 2, 2, foreground);
            }
        }
    }
}

static void render_all(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            render_cell(x, y);
        }
    }
}



int shell_is_open(void) {
    return window_open;
}

void shell_set_background_painter(void (*paint)(int x, int y, int w, int h)) {
    background_painter = paint;
}

void shell_open(void) {
    if (window_open || !fb_info.has_framebuffer) return;
    place_window();
    window_open = 1;
    draw_chrome("Terminal");
    render_all();
    gfx_present();
}

void shell_close(void) {
    if (!window_open) return;
    window_open = 0;
    dragging = 0;
    paint_background(window_x - 2, window_y - 2, window_width() + 8, window_height() + 10);
    gfx_present();
}

void shell_redraw(void) {
    if (!fb_info.has_framebuffer) return;
    if (!window_open) {
        shell_open();
        return;
    }
    draw_chrome("Terminal");
    render_all();
    gfx_present();
}



static void on_vga_event(int event, int x, int y) {
    if (!fb_info.has_framebuffer) return;      

    if (!window_open) {
        shell_open();                         
        return;
    }

    if (event == VGA_EV_CELL) render_cell(x, y);
    else render_all();
    gfx_present();
}

void shell_window_init(void) {
    vga_set_listener(on_vga_event);
}


void shell_window_mouse(int mouse_x, int mouse_y, int buttons) {
    const int left = (buttons & 1) != 0;
    const int pressed = left && !(prev_buttons & 1);
    prev_buttons = buttons;
    if (!window_open) return;

    if (pressed) {
        if (in_close_button(mouse_x, mouse_y)) {
            shell_close();
            return;
        }
        if (mouse_x >= window_x + 1 &&
            mouse_x < window_x + window_width() - CLOSE_SIZE - 12 &&
            mouse_y >= window_y && mouse_y < window_y + WINDOW_TITLE_HEIGHT) {
            dragging = 1;
            drag_dx = mouse_x - window_x;
            drag_dy = mouse_y - window_y;
        }
    }
    if (!left) dragging = 0;
    if (!dragging) return;

    const int old_x = window_x;
    const int old_y = window_y;
    window_x = mouse_x - drag_dx;
    window_y = mouse_y - drag_dy;
    const int max_x = (int)fb_info.framebuffer_width - window_width();
    const int max_y = (int)fb_info.framebuffer_height - TASKBAR_HEIGHT - window_height();
    if (window_x < 0) window_x = 0;
    if (window_y < 0) window_y = 0;
    if (window_x > max_x) window_x = max_x;
    if (window_y > max_y) window_y = max_y;
    if (window_y < 0) window_y = 0;

    if (window_x != old_x || window_y != old_y) {
        paint_background(old_x, old_y, window_width() + 7, window_height() + 9);
        draw_chrome("Terminal");
        render_all();
        gfx_present();
    }
}