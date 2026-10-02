#include "idt.h"
#include "vga.h"
#include "kernel.h"
#include "graphics.h"
#include "fat.h"
#include "explorer.h"

#define EX_MAX_ENTRIES 64
#define EX_NAME_LEN 13
#define EX_ROWS 7
#define EX_X 180
#define EX_Y 120
#define EX_W 560
#define EX_H 320
#define EX_TEXT_BUF 1024

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;
extern char keyboard_getchar_poll(void);

static char ex_files[EX_MAX_ENTRIES][EX_NAME_LEN];
static char ex_folders[EX_MAX_ENTRIES][EX_NAME_LEN];
static int ex_file_count;
static int ex_folder_count;
static char ex_path[256];
static int ex_sel_x;
static int ex_sel_y;
static int ex_files_top;
static int ex_folders_top;
static int ex_menu_focus;
static int ex_menu_open;
static int ex_menu_index;
static const char* ex_status = "";
static char ex_text_buf[EX_TEXT_BUF];

static void ex_text(int x, int y, const char* text, uint32_t color) {
    vga_draw_text_at(x, y, text, color);
}

static int ex_streq(const char* a, const char* b) {
    return strcmp(a, b) == 0;
}

static void ex_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ex_join(char* out, int size, const char* name) {
    int n = 0;
    for (int i = 0; ex_path[i]; i++) {
        if (n >= size - 1) return 0;
        out[n++] = ex_path[i];
    }
    if (n > 0 && out[n - 1] != '/') {
        if (n >= size - 1) return 0;
        out[n++] = '/';
    }
    for (int i = 0; name[i]; i++) {
        if (n >= size - 1) return 0;
        out[n++] = name[i];
    }
    out[n] = '\0';
    return 1;
}

static void ex_go_up(void) {
    int len = 0;
    while (ex_path[len]) len++;
    int i = len - 1;
    while (i > 0 && ex_path[i] != '/') i--;
    if (i <= 0) {
        ex_path[0] = '/';
        ex_path[1] = '\0';
    } else {
        ex_path[i] = '\0';
    }
}

static void ex_format_name(const FAT_DirectoryEntry* entry, char out[EX_NAME_LEN]) {
    int n = 0;
    for (int i = 0; i < 8 && entry->Name[i] != ' '; i++) out[n++] = (char)entry->Name[i];
    if (entry->Name[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && entry->Name[i] != ' '; i++) out[n++] = (char)entry->Name[i];
    }
    out[n] = '\0';
}

static void ex_reload(void) {
    ex_file_count = 0;
    ex_folder_count = 0;
    if (ex_path[1] != '\0') {
        ex_copy(ex_folders[0], "..", EX_NAME_LEN);
        ex_folder_count = 1;
    }

    FAT_File* dir = FAT_Open(&kernel_fat_disk, ex_path);
    if (dir == NULL) return;
    if (!dir->IsDirectory) {
        FAT_Close(dir);
        return;
    }

    FAT_DirectoryEntry entry;
    while (FAT_ReadEntry(&kernel_fat_disk, dir, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Name[0] == '.' ||
            entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID)) continue;

        if (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) {
            if (ex_folder_count < EX_MAX_ENTRIES) {
                ex_format_name(&entry, ex_folders[ex_folder_count++]);
            }
        } else if (ex_file_count < EX_MAX_ENTRIES) {
            ex_format_name(&entry, ex_files[ex_file_count++]);
        }
    }
    FAT_Close(dir);
}

static const char* ex_selected(void) {
    if (ex_sel_x == 0 && ex_sel_y < ex_file_count) return ex_files[ex_sel_y];
    if (ex_sel_x == 1 && ex_sel_y < ex_folder_count) return ex_folders[ex_sel_y];
    return 0;
}

static void ex_fix_selection(void) {
    if (ex_sel_x == 2) return;
    const int count = ex_sel_x == 0 ? ex_file_count : ex_folder_count;
    int* top = ex_sel_x == 0 ? &ex_files_top : &ex_folders_top;
    if (count == 0) ex_sel_y = 0;
    else if (ex_sel_y >= count) ex_sel_y = count - 1;
    if (ex_sel_y < 0) ex_sel_y = 0;
    if (ex_sel_y < *top) *top = ex_sel_y;
    if (ex_sel_y >= *top + EX_ROWS) *top = ex_sel_y - EX_ROWS + 1;
    if (*top < 0) *top = 0;
}

static void ex_select_name(int column, const char* name) {
    const int count = column == 0 ? ex_file_count : ex_folder_count;
    for (int i = 0; i < count; i++) {
        const char* item = column == 0 ? ex_files[i] : ex_folders[i];
        if (ex_streq(item, name)) {
            ex_sel_x = column;
            ex_sel_y = i;
            ex_fix_selection();
            return;
        }
    }
}

static void ex_display_path(char* out) {
    int n = 0;
    out[n++] = 'C';
    out[n++] = ':';
    if (ex_path[1] == '\0') {
        out[n++] = '\\';
    } else {
        int len = 0;
        while (ex_path[len]) len++;
        int start = len > 22 ? len - 22 : 0;
        for (int i = start; i < len && n < 126; i++) {
            out[n++] = ex_path[i] == '/' ? '\\' : ex_path[i];
        }
    }
    out[n] = '\0';
}

static void ex_draw_menu(void) {
    static const char* const options[] = {"CREATE FILE", "CREATE FOLDER", "OPEN", "DELETE"};
    const int x = 192;
    const int y = 164;
    if (ex_menu_focus) {
        draw_rect(x - 4, y - 20, 66, 20, 0xA0A0A0);
        ex_text(x, y - 17, "FILES", 0x000000);
    }
    if (!ex_menu_open) return;

    draw_rect(x - 6, y, 184, 92, 0x202020);
    draw_rect(x - 4, y + 2, 180, 88, 0xF0F0F0);
    for (int i = 0; i < 4; i++) {
        if (i == ex_menu_index) {
            draw_rect(x - 2, y + 4 + i * 21, 176, 20, 0x0050A0);
            ex_text(x + 4, y + 7 + i * 21, options[i], 0xFFFFFF);
        } else {
            ex_text(x + 4, y + 7 + i * 21, options[i], 0x000000);
        }
    }
}

static void ex_draw_column(int column, int px, int top) {
    const int count = column == 0 ? ex_file_count : ex_folder_count;
    for (int row = 0; row < EX_ROWS; row++) {
        const int index = top + row;
        if (index >= count) break;
        const char* name = column == 0 ? ex_files[index] : ex_folders[index];
        const int py = EX_Y + 68 + row * 26;
        if (ex_sel_x == column && ex_sel_y == index) {
            draw_rect(px, py, 150, 18, 0xB0B0B0);
            draw_rect(px + 1, py + 1, 148, 16, 0xD0D0D0);
        }
        ex_text(px + 4, py + 2, name, 0x000000);
    }
}

static void ex_draw(void) {
    const int x = EX_X;
    const int y = EX_Y;
    const int w = EX_W;
    const int h = EX_H;

    draw_rect(x - 4, y - 4, w + 8, h + 8, 0x202020);
    draw_rect(x, y, w, h, 0xC0C0C0);
    draw_rect(x, y, w, 22, 0x0050A0);
    ex_text(x + 20, y + 6, "FILE EXPLORER", 0xFFFFFF);
    draw_rect(x + w - 18, y + 6, 10, 10, ex_sel_x == 2 ? 0xFF0000 : 0xD04040);
    ex_text(x + w - 17, y + 7, "X", 0xFFFFFF);

    draw_rect(x, y + 22, w, 22, 0xC0C0C0);
    ex_text(x + 14, y + 27, "FILES", 0x000000);
    draw_rect(x + 12, y + 48, 160, h - 68, 0xD9D9D9);
    draw_rect(x + 182, y + 48, w - 194, h - 68, 0xF0F0F0);
    ex_text(x + 26, y + 54, "FILES", 0x000000);
    ex_text(x + 210, y + 54, "FOLDERS", 0x000000);

    ex_draw_column(0, x + 20, ex_files_top);
    ex_draw_column(1, x + 208, ex_folders_top);

    draw_rect(x + 12, y + h - 24, w - 24, 20, 0xD8D8D8);
    char shown[130];
    ex_display_path(shown);
    ex_text(x + 22, y + h - 18, shown, 0x000000);
    if (ex_status[0]) ex_text(x + 320, y + h - 18, ex_status, 0x800000);

    ex_draw_menu();
    gfx_present();
}

static void ex_view_file(const char* name) {
    char full[256];
    uint32_t length = 0;
    if (ex_join(full, sizeof(full), name)) {
        FAT_File* file = FAT_Open(&kernel_fat_disk, full);
        if (file != NULL && !file->IsDirectory) {
            while (length < EX_TEXT_BUF - 1) {
                const uint32_t count = FAT_Read(&kernel_fat_disk, file,
                                                EX_TEXT_BUF - 1 - length,
                                                ex_text_buf + length);
                if (count == 0) break;
                length += count;
            }
        }
        if (file != NULL) FAT_Close(file);
    }
    ex_text_buf[length] = '\0';

    const int x = EX_X;
    const int y = EX_Y;
    draw_rect(x - 4, y - 4, EX_W + 8, EX_H + 8, 0x202020);
    draw_rect(x, y, EX_W, EX_H, 0xC0C0C0);
    draw_rect(x, y, EX_W, 22, 0x0050A0);
    ex_text(x + 20, y + 6, "NEUTRA NOTEPAD", 0xFFFFFF);
    draw_rect(x + 12, y + 32, EX_W - 24, EX_H - 54, 0xFFFFFF);
    ex_text(x + 22, y + 40, name, 0x0000A0);

    if (length == 0) {
        ex_text(x + 22, y + 66, "(EMPTY)", 0x808080);
    } else {
        int col = 0;
        int row = 0;
        for (uint32_t i = 0; i < length; i++) {
            char c = ex_text_buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                row++;
                col = 0;
                if (row >= 10) break;
                continue;
            }
            if (c < 32 || c > 126) c = '.';
            if (col >= 42) {
                row++;
                col = 0;
            }
            if (row >= 10) break;
            char one[2] = {c, '\0'};
            ex_text(x + 22 + col * 12, y + 66 + row * 20, one, 0x000000);
            col++;
        }
    }

    draw_rect(x + 12, y + EX_H - 20, EX_W - 24, 16, 0xD8D8D8);
    ex_text(x + 22, y + EX_H - 18, "ESC OR ENTER TO CLOSE", 0x000000);
    gfx_present();

    while (1) {
        const char key = keyboard_getchar_poll();
        if (key == 27 || key == '\n' || key == 'q' || key == 'Q') return;
    }
}

static int ex_prompt_name(char* name, int is_directory) {
    const int x = 275;
    const int y = 228;
    int length = 0;
    name[0] = '\0';
    while (1) {
        draw_rect(x - 3, y - 3, 350, 92, 0x202020);
        draw_rect(x, y, 344, 86, 0xC0C0C0);
        draw_rect(x, y, 344, 20, 0x0050A0);
        ex_text(x + 10, y + 5, is_directory ? "CREATE FOLDER" : "CREATE FILE", 0xFFFFFF);
        ex_text(x + 12, y + 31, is_directory ? "FOLDER NAME:" : "FILE NAME:", 0x000000);
        draw_rect(x + 12, y + 47, 316, 20, 0xFFFFFF);
        ex_text(x + 18, y + 50, name, 0x000000);
        gfx_present();

        const char key = keyboard_getchar_poll();
        if (key == 27) return 0;
        if (key == '\n') return length > 0;
        if (key == '\b') {
            if (length > 0) name[--length] = '\0';
            continue;
        }
        char value = key;
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        if (((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
             value == '.' || value == '_' || value == '-') && length < 12) {
            name[length++] = value;
            name[length] = '\0';
        }
    }
}

static void ex_create(const char* name, int is_directory) {
    char full[256];
    if (!ex_join(full, sizeof(full), name)) {
        ex_status = "PATH TOO LONG";
        return;
    }
    FAT_File* existing = FAT_Open(&kernel_fat_disk, full);
    if (existing != NULL) {
        FAT_Close(existing);
        ex_status = "ALREADY EXISTS";
        return;
    }

    if (is_directory) {
        if (!FAT_Mkdir(&kernel_fat_disk, full)) {
            ex_status = "CREATE FAILED";
            return;
        }
    } else {
        FAT_File* file = FAT_Create(&kernel_fat_disk, full);
        if (file == NULL) {
            ex_status = "CREATE FAILED";
            return;
        }
        FAT_Close(file);
    }

    ex_status = "CREATED";
    ex_reload();
    ex_select_name(is_directory ? 1 : 0, name);
}

static void ex_delete_selected(void) {
    const char* selected = ex_selected();
    if (selected == 0 || selected[0] == '.') {
        ex_status = "NOTHING SELECTED";
        return;
    }

    char name[EX_NAME_LEN];
    ex_copy(name, selected, EX_NAME_LEN);
    const int was_folder = ex_sel_x == 1;

    char full[256];
    if (!ex_join(full, sizeof(full), name)) {
        ex_status = "PATH TOO LONG";
        return;
    }

    if (FAT_Remove(&kernel_fat_disk, full, 0)) {
        ex_status = "DELETED";
    } else {
        ex_status = was_folder ? "FOLDER NOT EMPTY" : "DELETE FAILED";
    }
    ex_reload();
    ex_fix_selection();
}

static int ex_open_selected(void) {
    const char* selected = ex_selected();
    if (selected == 0) return 0;

    if (ex_sel_x == 0) {
        ex_view_file(selected);
        return 1;
    }

    char came_from[EX_NAME_LEN];
    came_from[0] = '\0';
    if (ex_streq(selected, "..")) {
        int len = 0;
        while (ex_path[len]) len++;
        int i = len - 1;
        while (i > 0 && ex_path[i] != '/') i--;
        ex_copy(came_from, ex_path + i + 1, EX_NAME_LEN);
        ex_go_up();
    } else {
        char full[256];
        if (!ex_join(full, sizeof(full), selected)) return 0;
        ex_copy(ex_path, full, sizeof(ex_path));
    }

    ex_sel_x = 1;
    ex_sel_y = 0;
    ex_files_top = 0;
    ex_folders_top = 0;
    ex_reload();
    if (came_from[0]) ex_select_name(1, came_from);
    ex_fix_selection();
    return 1;
}

static void ex_run_action(int action) {
    char name[16];
    if (action == 0 || action == 1) {
        if (ex_prompt_name(name, action == 1)) ex_create(name, action == 1);
    } else if (action == 2) {
        ex_open_selected();
    } else {
        ex_delete_selected();
    }
    ex_fix_selection();
}

void explorer_run(void) {
    if (!kernel_fat_mounted) {
        vga_write("no mounted volume\n", MAKE_COLOR(COLOR_BLACK, COLOR_RED));
        return;
    }

    ex_path[0] = '/';
    ex_path[1] = '\0';
    ex_sel_x = 0;
    ex_sel_y = 0;
    ex_files_top = 0;
    ex_folders_top = 0;
    ex_menu_focus = 0;
    ex_menu_open = 0;
    ex_menu_index = 0;
    ex_status = "";
    ex_reload();
    ex_fix_selection();
    ex_draw();

    int previous_left_button = 0;
    while (1) {
        const char key = keyboard_try_getchar_poll();
        const int left_button = mouse_get_buttons() & 1;
        int mouse_x;
        int mouse_y;
        mouse_get_position(&mouse_x, &mouse_y);
        const int close_clicked = left_button && !previous_left_button &&
                                  mouse_x >= EX_X + EX_W - 18 && mouse_x < EX_X + EX_W - 8 &&
                                  mouse_y >= EX_Y + 6 && mouse_y < EX_Y + 16;
        previous_left_button = left_button;

        if (close_clicked || key == 'q' || key == 'Q' || key == 'x' || key == 'X') return;
        if (key == 27) {
            if (ex_menu_open || ex_menu_focus) {
                ex_menu_open = 0;
                ex_menu_focus = 0;
                ex_draw();
                continue;
            }
            return;
        }
        if ((key == ' ' || key == '\n') && ex_sel_x == 2) return;

        int dirty = 0;
        if (key != 0 && ex_status[0]) {
            ex_status = "";
            dirty = 1;
        }

        if (ex_menu_open) {
            if ((key == 'w' || key == 'W') && ex_menu_index > 0) {
                ex_menu_index--;
                dirty = 1;
            } else if ((key == 's' || key == 'S') && ex_menu_index < 3) {
                ex_menu_index++;
                dirty = 1;
            } else if (key == '\n') {
                const int action = ex_menu_index;
                ex_menu_open = 0;
                ex_menu_focus = 0;
                ex_run_action(action);
                dirty = 1;
            }
            if (dirty) ex_draw();
            continue;
        }

        if (ex_menu_focus) {
            if (key == '\n') {
                ex_menu_open = 1;
                ex_menu_index = 0;
                dirty = 1;
            } else if (key == 's' || key == 'S' || key == 'a' || key == 'A' ||
                       key == 'd' || key == 'D') {
                ex_menu_focus = 0;
                dirty = 1;
            }
            if (dirty) ex_draw();
            continue;
        }

        const int old_x = ex_sel_x;
        const int old_y = ex_sel_y;
        if (key == 'w' || key == 'W') {
            if (ex_sel_x != 2) {
                if (ex_sel_y == 0) ex_menu_focus = 1;
                else ex_sel_y--;
                dirty = 1;
            }
        } else if (key == 's' || key == 'S') {
            if (ex_sel_x != 2) ex_sel_y++;
        } else if (key == 'a' || key == 'A') {
            ex_sel_x = ex_sel_x == 0 ? 2 : (ex_sel_x == 2 ? 1 : 0);
        } else if (key == 'd' || key == 'D') {
            ex_sel_x = (ex_sel_x + 1) % 3;
        } else if (key == '\n') {
            if (ex_sel_x != 2 && ex_open_selected()) dirty = 1;
        } else if (key == '\b') {
            if (ex_sel_x != 2) {
                ex_delete_selected();
                dirty = 1;
            }
        }

        ex_fix_selection();
        if (dirty || ex_sel_x != old_x || ex_sel_y != old_y) ex_draw();
    }
}