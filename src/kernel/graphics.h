#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT_TAG_TYPE_END 0

typedef struct {
    uint32_t type;
    uint32_t size;
} MultibootTag;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} MultibootTagFramebuffer;

typedef struct {
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint8_t framebuffer_bpp;
    int has_framebuffer;
} FramebufferInfo;

extern FramebufferInfo fb_info;

void parse_multiboot_tags(uint32_t mbt_addr);
void putpixel_rgb(int x, int y, uint32_t color);
void draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void draw_rect(int x, int y, int width, int height, uint32_t color);
void fb_clear(uint32_t color);

void gfx_present(void);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void gfx_set_present_hook(void (*hook)(void));
void gfx_restore_rect(int x, int y, int w, int h);
void gfx_fb_putpixel(int x, int y, uint32_t color);
void gfx_draw_span(int x, int y, int count, const uint32_t* pixels);

#endif