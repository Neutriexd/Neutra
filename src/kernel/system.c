#include "system.h"

#define SYSTEM_BMP_MAX_DIMENSION 8192

static uint32_t system_draw_color = 0xFFFFFF;
static uint32_t system_bmp_row[SYSTEM_BMP_MAX_DIMENSION];

static uint16_t read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void set_color(uint32_t color) {
    system_draw_color = color & 0xFFFFFF;
}

uint32_t get_color(void) {
    return system_draw_color;
}

void putpixel(int x, int y) {
    putpixel_rgb(x, y, system_draw_color);
}

void line(int x0, int y0, int x1, int y1) {
    draw_line(x0, y0, x1, y1, system_draw_color);
}

void rect(int x, int y, int width, int height) {
    draw_rect(x, y, width, height, system_draw_color);
}

void clear_screen(uint32_t color) {
    fb_clear(color & 0xFFFFFF);
}

void present(void) {
    gfx_present();
}

int display_bmp(const void* bmp_data, uint32_t bmp_length,
                int x, int y, int width, int height, int size) {
    if (bmp_data == NULL || bmp_length < 54 || size < 0 || !fb_info.has_framebuffer) return 0;

    const uint8_t* bmp = (const uint8_t*)bmp_data;
    if (bmp[0] != 'B' || bmp[1] != 'M') return 0;

    const uint32_t pixel_offset = read_u32(bmp + 10);
    const uint32_t dib_size = read_u32(bmp + 14);
    if (dib_size < 40 || (uint64_t)14 + dib_size > bmp_length || pixel_offset < 14 + dib_size) return 0;

    const int32_t source_width = (int32_t)read_u32(bmp + 18);
    const int32_t signed_height = (int32_t)read_u32(bmp + 22);
    const uint16_t planes = read_u16(bmp + 26);
    const uint16_t bits_per_pixel = read_u16(bmp + 28);
    const uint32_t compression = read_u32(bmp + 30);
    if (source_width <= 0 || signed_height == 0 || signed_height == (-2147483647 - 1) ||
        source_width > SYSTEM_BMP_MAX_DIMENSION || planes != 1 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32) || compression != 0) return 0;

    const int source_height = signed_height < 0 ? -signed_height : signed_height;
    if (source_height > SYSTEM_BMP_MAX_DIMENSION) return 0;

    const uint32_t row_bits = (uint32_t)source_width * bits_per_pixel;
    const uint32_t source_stride = ((row_bits + 31u) / 32u) * 4u;
    const uint64_t image_end = (uint64_t)pixel_offset + source_stride * (uint32_t)source_height;
    if (image_end > bmp_length) return 0;

    int target_width;
    int target_height;
    if (size > 0) {
        target_width = size;
        target_height = size;
    } else if (width <= 0 && height <= 0) {
        target_width = source_width;
        target_height = source_height;
    } else if (width <= 0) {
        target_height = height;
        target_width = (source_width * height) / source_height;
    } else if (height <= 0) {
        target_width = width;
        target_height = (source_height * width) / source_width;
    } else {
        target_width = width;
        target_height = height;
    }
    if (target_width <= 0 || target_height <= 0 ||
        target_width > SYSTEM_BMP_MAX_DIMENSION || target_height > SYSTEM_BMP_MAX_DIMENSION) return 0;

    const uint32_t bytes_per_pixel = bits_per_pixel / 8;
    for (int target_y = 0; target_y < target_height; target_y++) {
        const int source_y = (target_y * source_height) / target_height;
        const int stored_y = signed_height > 0 ? source_height - 1 - source_y : source_y;
        const uint8_t* source_row = bmp + pixel_offset + (uint64_t)(uint32_t)stored_y * source_stride;
        for (int target_x = 0; target_x < target_width; target_x++) {
            const int source_x = (target_x * source_width) / target_width;
            const uint8_t* pixel = source_row + (uint32_t)source_x * bytes_per_pixel;
            system_bmp_row[target_x] = ((uint32_t)pixel[2] << 16) |((uint32_t)pixel[1] << 8) | pixel[0];
        }
        gfx_draw_span(x, y + target_y, target_width, system_bmp_row);
    }
    gfx_present();
    return 1;
}