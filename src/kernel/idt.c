#include "idt.h"
#include "vga.h"
#include "kernel.h"
#include "graphics.h"

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a" (result) : "Nd" (port));
    return result;
}

void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a" (value), "Nd" (port));
}

static const uint8_t scancode_to_ascii[128] = {
    0,    0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '#', 'y', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '-', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;
static int mouse_enabled;
static int keyboard_control_pressed;
static int keyboard_shift_pressed;
static int mouse_x = 0;
static int mouse_y = 0;
static uint8_t mouse_buttons;

typedef struct {
    int x_min, y_min, x_max, y_max;
} hitbox_t;

static int hitbox_contains(const hitbox_t* box, int x, int y) {
    return x >= box->x_min && x <= box->x_max && 
           y >= box->y_min && y <= box->y_max;
}

static void mouse_clamp_position(void) {
    if (!fb_info.has_framebuffer) return;

    const int max_x = (int)fb_info.framebuffer_width - 1;
    const int max_y = (int)fb_info.framebuffer_height - 1;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > max_x) mouse_x = max_x;
    if (mouse_y > max_y) mouse_y = max_y;
}

static int wait_input_buffer_empty(void) {
    for (int timeout = 0; timeout < 100000; timeout++) {
        if ((inb(KEYBOARD_STATUS_PORT) & 2) == 0) return 1;
    }
    return 0;
}

static int mouse_send(uint8_t value) {
    if (!wait_input_buffer_empty()) return 0;
    outb(KEYBOARD_STATUS_PORT, 0xD4);
    if (!wait_input_buffer_empty()) return 0;
    outb(KEYBOARD_DATA_PORT, value);
    return 1;
}

static int mouse_wait_ack(void) {
    for (int timeout = 0; timeout < 100000; timeout++) {
        const uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if (status & 1) {
            const uint8_t value = inb(KEYBOARD_DATA_PORT);
            if (status & 0x20) return value == 0xFA;
        }
    }
    return 0;
}

static void mouse_init(void) {
    mouse_enabled = 0;
    mouse_packet_index = 0;
    mouse_buttons = 0;
    if (!wait_input_buffer_empty()) return;
    outb(KEYBOARD_STATUS_PORT, 0xA8);
    if (!mouse_send(0xF6) || !mouse_wait_ack()) return;
    if (!mouse_send(0xF4) || !mouse_wait_ack()) return;
    mouse_enabled = 1;
    mouse_x = fb_info.has_framebuffer ? (int)(fb_info.framebuffer_width / 2) : 0;
    mouse_y = fb_info.has_framebuffer ? (int)(fb_info.framebuffer_height / 2) : 0;
    mouse_buttons = 0;
}

static void mouse_poll(void) {
    if (!mouse_enabled) return;

    for (int drained = 0; drained < 16; drained++) {
        const uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if ((status & 1) == 0 || (status & 0x20) == 0) return;

        const uint8_t value = inb(KEYBOARD_DATA_PORT);
        
        if (mouse_packet_index == 0) {
            if ((value & 0x08) == 0) continue;
        }
        
        mouse_packet[mouse_packet_index++] = value;
        
        if (mouse_packet_index == 3) {
            mouse_packet_index = 0;
            
            if ((mouse_packet[0] & 0xC0) == 0) {
                const int delta_x = (int8_t)mouse_packet[1];
                const int delta_y = -(int8_t)mouse_packet[2];
                
                mouse_x += delta_x;
                mouse_y += delta_y;
                mouse_clamp_position();
                mouse_buttons = mouse_packet[0] & 7;
                vga_mouse_update(delta_x, delta_y, mouse_buttons);
            }
        }
    }
}

int mouse_hitbox_check(int x_min, int y_min, int x_max, int y_max) {
    hitbox_t box = {x_min, y_min, x_max, y_max};
    return hitbox_contains(&box, mouse_x, mouse_y);
}

void mouse_get_position(int* out_x, int* out_y) {
    if (out_x) *out_x = mouse_x;
    if (out_y) *out_y = mouse_y;
}

int mouse_get_buttons(void) {
    mouse_poll();
    return mouse_buttons;
}

void mouse_set_position(int x, int y) {
    const int old_x = mouse_x;
    const int old_y = mouse_y;
    mouse_x = x;
    mouse_y = y;
    mouse_clamp_position();
    vga_mouse_update(mouse_x - old_x, mouse_y - old_y, mouse_buttons);
}

void idt_init(void) {
    vga_write("polling\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
}

void keyboard_handler_install(void) {
    mouse_init();
    vga_write("keyboard polling\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
    if (mouse_enabled) {
        vga_write("mouse polling\n", MAKE_COLOR(COLOR_BLACK, COLOR_GREEN));
    }
}

char keyboard_try_getchar_poll(void) {
    mouse_poll();
    const uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if ((status & 1) == 0 || (status & 0x20) != 0) return 0;

    const uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    if (scancode == 0x2A || scancode == 0x36) {
        keyboard_shift_pressed = 1;
        return 0;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        keyboard_shift_pressed = 0;
        return 0;
    }
    if (scancode == 0x1D) {
        keyboard_control_pressed = 1;
        return 0;
    }
    if (scancode == 0x9D) {
        keyboard_control_pressed = 0;
        return 0;
    }
    if (scancode >= 128) return 0;

    char c = scancode_to_ascii[scancode];
    if (c == 0) return 0;
    if (keyboard_control_pressed && c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 1);
    }
    if (keyboard_shift_pressed) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        else if (c >= '1' && c <= '6') c = "!\"#$%&"[c - '1'];
        else if (c == '7') c = '/';
        else if (c == '8') c = '(';
        else if (c == '9') c = ')';
        else if (c == '0') c = '=';
        else if (c == '-') c = '_';
        else if (c == '=') c = '*';
        else if (c == '[') c = '{';
        else if (c == ']') c = '}';
        else if (c == ';') c = ':';
        else if (c == '\'') c = '"';
        else if (c == '`') c = '^';
        else if (c == '#') c = '\'';
        else if (c == ',') c = '<';
        else if (c == '.') c = '>';
        else if (c == '/') c = '?';
    }
    return c;
}

char keyboard_getchar_poll(void) {
    char c;
    while ((c = keyboard_try_getchar_poll()) == 0) {
        __asm__ volatile("pause");
    }
    return c;
}