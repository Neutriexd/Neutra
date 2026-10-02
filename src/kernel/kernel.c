#include <stdint.h>
#include <stddef.h>
#include "kernel.h"
#include "scheduler.h"
#include "vga.h"
#include "idt.h"
#include "shell.h"
#include "shell_window.h"
#include "dce.h"
#include "graphics.h"
#include "disk.h"
#include "fat.h"
#include "desktop.h"
#include "kernel_memory.h"
#include "kernel_memory/program.h"
#include "taskbar.h"
#include "system.h"
uint32_t mbt_addr = 0;
bool DISK_InitializeAta(DISK* disk);
void redraw_desk_region(int x, int y, int width, int height);
void cursor_init(void);
DISK kernel_fat_disk;
int kernel_fat_mounted;

typedef struct {
    uint32_t type;
    uint32_t size;
} MultibootTagHeader;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t start;
    uint32_t end;
    char name[];
} MultibootModuleTag;

static int find_fat_module(uint32_t info_address, uint32_t* start, uint32_t* end) {
    if (info_address == 0 || start == NULL || end == NULL) return 0;

    const uint32_t total_size = *(const uint32_t*)(uintptr_t)info_address;
    if (total_size < 16) return 0;

    uint32_t offset = 8;
    while (offset <= total_size - sizeof(MultibootTagHeader)) {
        const MultibootTagHeader* header =
            (const MultibootTagHeader*)(uintptr_t)(info_address + offset);
        if (header->size < sizeof(MultibootTagHeader) ||
            header->size > total_size - offset) return 0;
        if (header->type == 0) return 0;

        if (header->type == 3 && header->size >= sizeof(MultibootModuleTag)) {
            const MultibootModuleTag* module =
                (const MultibootModuleTag*)(uintptr_t)(info_address + offset);
            const char* name = module->name;
            const char expected[] = "fatdisk";
            uint32_t i = 0;
            while (i < sizeof(expected) - 1 && name[i] == expected[i]) i++;
            if (i == sizeof(expected) - 1 && name[i] == '\0' && module->end >= module->start) {
                *start = module->start;
                *end = module->end;
                return 1;
            }
        }

        offset += (header->size + 7) & ~7u;
    }
    return 0;
}

void int_to_str(int num, char* buf, int buf_size) {
    if (buf_size < 2) return;
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int is_negative = (num < 0);
    if (is_negative) num = -num;
    int i = 0;
    int temp = num;
    while (temp > 0 && i < buf_size - 2) {
        buf[i++] = '0' + (temp % 10);
        temp /= 10;
    }
    if (is_negative && i < buf_size - 2) {
        buf[i++] = '-';
    }
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
}

void hex_to_str(unsigned int num, char* buf, int buf_size) {
    if (buf_size < 3) return;
    const char* hex_chars = "0123456789abcdef";
    int i = 0;
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (num > 0 && i < buf_size - 1) {
        buf[i++] = hex_chars[num & 0xF];
        num >>= 4;
    }
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
}

void memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
}

void memcpy(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; i++) {
        d[i] = s[i];
    }
}

int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (unsigned char)*str1 - (unsigned char)*str2;
}

void idt_task(void) {
    idt_init();
    keyboard_handler_install();
}
void shell_task(void) {
    shell_run();
}
void init_taskb(void) {
    init_taskbar();
}
void run_program(void) {

    DCE_VM vm;
    dce_vm_init(&vm);

    dce_vm_load_cde(&vm, program_cde, program_cde_len);
    const CDE_Header* h = (const CDE_Header*)program_cde;


    dce_vm_execute(&vm);


}

static int cde_started = 0;

void execute_cde_task(void) {
    if (cde_started)
        return;

    cde_started = 1;
    run_program();
}

void test_framebuffer(void) {
    if (!fb_info.has_framebuffer) {
        vga_write("no framebuffer available\n", MAKE_COLOR(COLOR_BLACK, COLOR_RED));
        return;
    }

    const int center_x = (int)(fb_info.framebuffer_width / 2);
    const int center_y = (int)(fb_info.framebuffer_height / 2);
    const int frame_half_width = fb_info.framebuffer_width < 720 ? 140 : 300;
    const int frame_half_height = fb_info.framebuffer_height < 500 ? 100 : 210;
    const int left = center_x - frame_half_width;
    const int right = center_x + frame_half_width;
    const int top = center_y - frame_half_height;
    const int bottom = center_y + frame_half_height;

    fb_clear(0x080D18);

    draw_line(left, top, right, top, 0x35D6C8);
    draw_line(right, top, right, bottom, 0x35D6C8);
    draw_line(right, bottom, left, bottom, 0x35D6C8);
    draw_line(left, bottom, left, top, 0x35D6C8);
    draw_line(left, top + 32, right, top + 32, 0x31506A);

    const int size = fb_info.framebuffer_height < 500 ? 38 : 78;
    const int depth = size * 3 / 5;
    const int front_left = center_x - size / 2;
    const int front_top = center_y - size / 2 + depth / 2;
    const int front_right = front_left + size;
    const int front_bottom = front_top + size;
    const int back_left = front_left + depth;
    const int back_top = front_top - depth;
    const int back_right = front_right + depth;
    const int back_bottom = front_bottom - depth;
    const uint32_t cube_color = 0xF2C14E;

    draw_line(front_left, front_top, front_right, front_top, cube_color);
    draw_line(front_right, front_top, front_right, front_bottom, cube_color);
    draw_line(front_right, front_bottom, front_left, front_bottom, cube_color);
    draw_line(front_left, front_bottom, front_left, front_top, cube_color);
    draw_line(back_left, back_top, back_right, back_top, cube_color);
    draw_line(back_right, back_top, back_right, back_bottom, cube_color);
    draw_line(back_right, back_bottom, back_left, back_bottom, cube_color);
    draw_line(back_left, back_bottom, back_left, back_top, cube_color);
    draw_line(front_left, front_top, back_left, back_top, cube_color);
    draw_line(front_right, front_top, back_right, back_top, cube_color);
    draw_line(front_right, front_bottom, back_right, back_bottom, cube_color);
    draw_line(front_left, front_bottom, back_left, back_bottom, cube_color);

    vga_write("done\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
}

void kernel_entry(uint32_t mbt_addr) {
    shell_window_init();                                   
    shell_set_background_painter(redraw_desk_region);      
    vga_clear();
    vga_write("Neutra Kernel\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
    
    parse_multiboot_tags(mbt_addr);
    init_desk();
    cursor_init();                                         
    shell_redraw();                                        
    kernel_memory_init(mbt_addr);

    uint32_t module_start;
    uint32_t module_end;
    kernel_fat_mounted = 0;

    if (DISK_InitializeAta(&kernel_fat_disk) && FAT_Initialize(&kernel_fat_disk)) {
        kernel_fat_mounted = 1;
        vga_write("ata disk mounted\n",MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
    }
    else if (find_fat_module(mbt_addr, &module_start, &module_end) &&DISK_Initialize(&kernel_fat_disk, (void*)(uintptr_t)module_start,module_end - module_start) &&FAT_Initialize(&kernel_fat_disk)) {
        kernel_fat_mounted = 1;
        vga_write("disk mounted\n",MAKE_COLOR(COLOR_BLACK, COLOR_LIGHT_YELLOW));
    } else {
        vga_write("no ata or grub disk mod\n",MAKE_COLOR(COLOR_BLACK, COLOR_RED));
    }
    
    scheduler_init();
    scheduler_add_task("idt", idt_task, 10);
    scheduler_add_task("Taskbar", init_taskb, 10);
    scheduler_add_task("Shell", shell_task, 10);
    scheduler_run();
    
    vga_write("Kernel halted\n", MAKE_COLOR(COLOR_BLACK, COLOR_CYAN));
    
    while (1) {
        __asm__ volatile("hlt");
    }
}