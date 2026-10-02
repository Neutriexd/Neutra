#include "graphics.h"
#include "vga.h"

#define GFX_MAX_W 1920
#define GFX_MAX_H 1080

FramebufferInfo fb_info = {0};

static uint32_t gfx_back[GFX_MAX_W * GFX_MAX_H];
static int gfx_inited;
static int gfx_buffered;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1, dirty_valid;
static void (*gfx_present_hook)(void);

void parse_multiboot_tags(uint32_t mbt_addr) {
    if (mbt_addr == 0) return;

    const uint32_t total_size = *(const uint32_t*)mbt_addr;
    if (total_size < 16) return;
    
    vga_write("Parse tags at 0x", MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    vga_print_hex(mbt_addr, MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    vga_write("\n", MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    
    uint32_t offset = 8;
    while (offset <= total_size - sizeof(MultibootTag)) {
        const MultibootTag* tag = (const MultibootTag*)(mbt_addr + offset);
        if (tag->size < sizeof(MultibootTag) || tag->size > total_size - offset) {
            break;
        }
        if (tag->type == MULTIBOOT_TAG_TYPE_END) break;

        vga_write("Tag type=", MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
        vga_print_int(tag->type, MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
        vga_write(" size=", MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
        vga_print_int(tag->size, MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
        vga_write("\n", MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
        
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER &&
            tag->size >= sizeof(MultibootTagFramebuffer)) {
            const MultibootTagFramebuffer* fb = 
                (const MultibootTagFramebuffer*)tag;
            const uint32_t framebuffer_address =
                (uint32_t)fb->framebuffer_addr;

            if (fb->framebuffer_bpp == 32 && fb->framebuffer_type == 1 &&
                fb->framebuffer_width > 0 && fb->framebuffer_height > 0 &&
                fb->framebuffer_width <= 0x3FFFFFFFu &&
                fb->framebuffer_pitch / 4 >= fb->framebuffer_width &&
                (fb->framebuffer_pitch % 4) == 0 &&
                (fb->framebuffer_addr >> 32) == 0 &&
                fb->framebuffer_pitch <=
                    (0xFFFFFFFFu - framebuffer_address) /
                        fb->framebuffer_height) {
                fb_info.framebuffer_addr = fb->framebuffer_addr;
                fb_info.framebuffer_width = fb->framebuffer_width;
                fb_info.framebuffer_height = fb->framebuffer_height;
                fb_info.framebuffer_pitch = fb->framebuffer_pitch;
                fb_info.framebuffer_bpp = fb->framebuffer_bpp;
                fb_info.has_framebuffer = 1;

                vga_write("FB FOUND!\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
            }
        }

        const uint32_t next_offset = offset + ((tag->size + 7) & ~7u);
        if (next_offset <= offset || next_offset > total_size) break;
        offset = next_offset;
    }
}

static volatile uint32_t* fb_row(int y) {
    return (volatile uint32_t*)(uintptr_t)(fb_info.framebuffer_addr +
                                           (uint64_t)y * fb_info.framebuffer_pitch);
}

static void copy_words(volatile uint32_t* destination, const uint32_t* source, int count) {
    uint32_t* d = (uint32_t*)destination;
    const uint32_t* s = source;
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(count) : : "memory");
}

static void gfx_init(void) {
    if (gfx_inited) return;
    if (!fb_info.has_framebuffer || fb_info.framebuffer_bpp != 32) return;
    gfx_inited = 1;

    const int w = (int)fb_info.framebuffer_width;
    const int h = (int)fb_info.framebuffer_height;
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = w;
    clip_y1 = h;

    if (w <= GFX_MAX_W && h <= GFX_MAX_H) {
        for (int y = 0; y < h; y++) {
            copy_words((volatile uint32_t*)(gfx_back + (uint32_t)y * (uint32_t)w),(const uint32_t*)fb_row(y), w);
        }
        gfx_buffered = 1;
    }
}

static void gfx_mark(int x0, int y0, int x1, int y1) {
    if (!gfx_buffered) return;
    if (!dirty_valid) {
        dirty_x0 = x0; dirty_y0 = y0; dirty_x1 = x1; dirty_y1 = y1;
        dirty_valid = 1;
        return;
    }
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

void gfx_set_clip(int x, int y, int w, int h) {
    gfx_init();
    if (!gfx_inited) return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb_info.framebuffer_width) x1 = (int)fb_info.framebuffer_width;
    if (y1 > (int)fb_info.framebuffer_height) y1 = (int)fb_info.framebuffer_height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1;
}

void gfx_reset_clip(void) {
    gfx_init();
    if (!gfx_inited) return;
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = (int)fb_info.framebuffer_width;
    clip_y1 = (int)fb_info.framebuffer_height;
}

void gfx_set_present_hook(void (*hook)(void)) {
    gfx_present_hook = hook;
}

void gfx_present(void) {
    gfx_init();
    if (!gfx_inited) return;

    if (gfx_buffered && dirty_valid) {
        const int w = (int)fb_info.framebuffer_width;
        const int h = (int)fb_info.framebuffer_height;
        int x0 = dirty_x0 < 0 ? 0 : dirty_x0;
        int y0 = dirty_y0 < 0 ? 0 : dirty_y0;
        int x1 = dirty_x1 > w ? w : dirty_x1;
        int y1 = dirty_y1 > h ? h : dirty_y1;
        dirty_valid = 0;
        if (x1 > x0) {
            for (int y = y0; y < y1; y++) {
                copy_words(fb_row(y) + x0, gfx_back + (uint32_t)y * (uint32_t)w + x0, x1 - x0);
            }
        }
    }
    if (gfx_present_hook) gfx_present_hook();
}

void gfx_restore_rect(int x, int y, int w, int h) {
    if (!gfx_inited || !gfx_buffered) return;
    const int sw = (int)fb_info.framebuffer_width;
    const int sh = (int)fb_info.framebuffer_height;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w;
    int y1 = y + h > sh ? sh : y + h;
    if (x1 <= x0) return;
    for (int yy = y0; yy < y1; yy++) {
        copy_words(fb_row(yy) + x0, gfx_back + (uint32_t)yy * (uint32_t)sw + x0, x1 - x0);
    }
}

void gfx_fb_putpixel(int x, int y, uint32_t color) {
    if (!fb_info.has_framebuffer || fb_info.framebuffer_bpp != 32) return;
    if (x < 0 || x >= (int)fb_info.framebuffer_width) return;
    if (y < 0 || y >= (int)fb_info.framebuffer_height) return;
    fb_row(y)[x] = color;
}

void putpixel_rgb(int x, int y, uint32_t color) {
    gfx_init();
    if (!gfx_inited) return;
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) return;

    if (gfx_buffered) {
        gfx_back[(uint32_t)y * fb_info.framebuffer_width + (uint32_t)x] = color;
        gfx_mark(x, y, x + 1, y + 1);
    } else {
        fb_row(y)[x] = color;
    }
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (1) {
        putpixel_rgb(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        const int doubled_error = 2 * error;
        if (doubled_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void draw_rect(int x, int y, int width, int height, uint32_t color) {
    gfx_init();
    if (!gfx_inited) return;

    int x0 = x, y0 = y, x1 = x + width, y1 = y + height;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1) return;

    const uint32_t stride = fb_info.framebuffer_width;
    for (int yy = y0; yy < y1; yy++) {
        if (gfx_buffered) {
            uint32_t* p = gfx_back + (uint32_t)yy * stride + (uint32_t)x0;
            for (int xx = x0; xx < x1; xx++) *p++ = color;
        } else {
            volatile uint32_t* p = fb_row(yy) + x0;
            for (int xx = x0; xx < x1; xx++) *p++ = color;
        }
    }
    gfx_mark(x0, y0, x1, y1);
}

void gfx_draw_span(int x, int y, int count, const uint32_t* pixels) {
    gfx_init();
    if (!gfx_inited || pixels == 0) return;
    if (y < clip_y0 || y >= clip_y1) return;

    int skip = 0;
    int x0 = x;
    int x1 = x + count;
    if (x0 < clip_x0) { skip = clip_x0 - x0; x0 = clip_x0; }
    if (x1 > clip_x1) x1 = clip_x1;
    if (x0 >= x1) return;

    if (gfx_buffered) {
        uint32_t* p = gfx_back + (uint32_t)y * fb_info.framebuffer_width + (uint32_t)x0;
        for (int xx = x0; xx < x1; xx++) *p++ = pixels[skip + (xx - x0)];
        gfx_mark(x0, y, x1, y + 1);
    } else {
        volatile uint32_t* p = fb_row(y) + x0;
        for (int xx = x0; xx < x1; xx++) *p++ = pixels[skip + (xx - x0)];
    }
}

void fb_clear(uint32_t color) {
    if (!fb_info.has_framebuffer) return;
    draw_rect(0, 0, (int)fb_info.framebuffer_width,(int)fb_info.framebuffer_height, color);
}