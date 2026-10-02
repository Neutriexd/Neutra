#include "system.h"
#include "taskbar.h"
#include "kernel.h"
#include "idt.h"
#include <stdint.h>
#include "stdbool.h"



void init_taskbar(void) {
    if (!fb_info.has_framebuffer) return;
    const int width = (int)fb_info.framebuffer_width;
    const int height = (int)fb_info.framebuffer_height;
    draw_rect(0, height - 36, width, 36, 0xA09C96);
    gfx_present();

}

void check_menu_click(void)
{
    const int width = (int)fb_info.framebuffer_width;
    const int height = (int)fb_info.framebuffer_height;

    if (mouse_hitbox_check(0, height - 36, width, 36)) {
        
    }
}

void taskbar_poll(void) {
    check_menu_click();
}

void run_taskbar(void){
    keep_alive();
}


void keep_alive(void) {
    while (1) {
        check_menu_click();
    }
}