#include "desktop.h"
#include "vga.h"
#include "graphics.h"


void redraw_desk_region(int x, int y, int width, int height) {
	if (!fb_info.has_framebuffer || width <= 0 || height <= 0) return;

	const int screen_width = (int)fb_info.framebuffer_width;
	const int screen_height = (int)fb_info.framebuffer_height;

	gfx_set_clip(x, y, width, height);
	draw_rect(x, y, width, height, 0x0878A8);
	for (int stripe_y = screen_height / 2; stripe_y < screen_height - 36; stripe_y += 2) {
		const uint32_t shade = 0x0878A8 + (uint32_t)((stripe_y - screen_height / 2) / 20);
		draw_rect(0, stripe_y, screen_width, 2, shade);
	}
	gfx_reset_clip();
}

void init_desk(void) {
	if (!fb_info.has_framebuffer) return;

	const int width = (int)fb_info.framebuffer_width;
	const int height = (int)fb_info.framebuffer_height;
	redraw_desk_region(0, 0, width, height);
	gfx_present();
}