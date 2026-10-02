#include "shell.h"
#include "shell_window.h"
#include "idt.h"
#include "vga.h"
#include "kernel.h"
#include "graphics.h"
#include "dce.h"
#include "fat.h"
#include "explorer.h"
#include "kernel_memory.h"

#define CMD_BUFFER_SIZE 256
#define NANO_BUFFER_SIZE 2048
#define SH_COLOR(c) MAKE_COLOR(COLOR_BLACK, (c))

static char cmd_buffer[CMD_BUFFER_SIZE];
static char current_dir[256] = "/";  
static char nano_buffer[NANO_BUFFER_SIZE];

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;
extern char keyboard_getchar_poll(void);
extern FramebufferInfo fb_info;

int strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return 1;
        if (s1[i] == '\0') return 0;
    }
    return 0;
}


static inline void shutdown_outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void system_shutdown(void) {
    vga_clear();
    vga_write("bye\n", SH_COLOR(COLOR_GREEN));
    shutdown_outw(0x604, 0x2000);    
    shutdown_outw(0xB004, 0x2000);  
    shutdown_outw(0x4004, 0x3400);   
    
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}



static void resolve_path(const char* arg, char* out) {
    char full[512];
    int n = 0;
    if (arg[0] != '/') {
        for (int i = 0; current_dir[i] && n < 255; i++) full[n++] = current_dir[i];
    }
    full[n++] = '/';
    for (int i = 0; arg[i] && n < 510; i++) full[n++] = arg[i];
    full[n] = '\0';

    int len = 1;
    out[0] = '/';
    out[1] = '\0';
    const char* p = full;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[16];
        int cl = 0;
        while (*p && *p != '/') {
            if (cl < 15) comp[cl++] = *p;
            p++;
        }
        comp[cl] = '\0';
        if (strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            int i = len - 1;
            while (i > 0 && out[i] != '/') i--;
            len = i > 0 ? i : 1;
            out[len] = '\0';
            continue;
        }
        if (len + 1 + cl >= 255) break;
        if (len > 1) out[len++] = '/';
        for (int i = 0; i < cl; i++) out[len++] = comp[i];
        out[len] = '\0';
    }
}

static int require_fat(void) {
    if (!kernel_fat_mounted) {
        vga_write("FAT: no mounted volume\n", SH_COLOR(COLOR_RED));
        return 0;
    }
    return 1;
}



void shell_init(void) {
    vga_clear();
    vga_write("Type 'help' for commands\n\n", SH_COLOR(COLOR_WHITE));
}

void shell_prompt(void) {
    vga_write("neutra@kernel:", SH_COLOR(COLOR_GREEN));
    vga_write(current_dir, SH_COLOR(COLOR_CYAN));
    vga_write("$ ", SH_COLOR(COLOR_WHITE));
}

extern void taskbar_poll(void);


static void shell_idle(void) {
    int mouse_x;
    int mouse_y;
    mouse_get_position(&mouse_x, &mouse_y);
    shell_window_mouse(mouse_x, mouse_y, mouse_get_buttons());
    taskbar_poll();
}

void shell_read_line(char* buffer, int size) {
    int i = 0;
    while (i < size - 1) {
        char c;
        while ((c = keyboard_try_getchar_poll()) == 0) shell_idle();
        if (c == '\n') {
            buffer[i] = '\0';
            vga_putchar('\n', SH_COLOR(COLOR_WHITE));
            return;
        }
        buffer[i++] = c;
        vga_putchar(c, SH_COLOR(COLOR_WHITE));
    }
    buffer[i] = '\0';
}

static void cmd_whoami(void) {
    vga_write("root\n", SH_COLOR(COLOR_WHITE));
}

static void cmd_pwd(void) {
    vga_write(current_dir, SH_COLOR(COLOR_WHITE));
    vga_write("\n", SH_COLOR(COLOR_WHITE));
}

static void cmd_echo(const char* text) {
    vga_write(text, SH_COLOR(COLOR_WHITE));
    vga_write("\n", SH_COLOR(COLOR_WHITE));
}

static void cmd_uname(void) {
    vga_write("Neutra OS 0.8\n", SH_COLOR(COLOR_WHITE));
}

static void cmd_execute(void) {
    extern void execute_cde_task(void);
    execute_cde_task();
}

static void cmd_help(void) {
    vga_write("Available commands:\n", SH_COLOR(COLOR_GREEN));
    vga_write("  whoami          Show current user\n", SH_COLOR(COLOR_WHITE));
    vga_write("  pwd             Print working directory\n", SH_COLOR(COLOR_WHITE));
    vga_write("  echo            Print text\n", SH_COLOR(COLOR_WHITE));
    vga_write("  uname           System info\n", SH_COLOR(COLOR_WHITE));
    vga_write("  execute         Run CDE programs\n", SH_COLOR(COLOR_WHITE));
    vga_write("  ls [-la|-lh]    List FAT directory\n", SH_COLOR(COLOR_WHITE));
    vga_write("  cd <dir>        Change directory\n", SH_COLOR(COLOR_WHITE));
    vga_write("  cat <file>      Print a FAT file\n", SH_COLOR(COLOR_WHITE));
    vga_write("  cp <src> <dst>  Copy file contents to FAT\n", SH_COLOR(COLOR_WHITE));
    vga_write("                  kernel_memory/<file> reads project sources\n", SH_COLOR(COLOR_WHITE));
    vga_write("  mkdir <dir>     Create a directory\n", SH_COLOR(COLOR_WHITE));
    vga_write("  nano <file>     Edit and save a FAT file\n", SH_COLOR(COLOR_WHITE));
    vga_write("  rm <file>       Remove a file or empty directory\n", SH_COLOR(COLOR_WHITE));
    vga_write("  rm -rf <path>   Remove a path recursively\n", SH_COLOR(COLOR_WHITE));
    vga_write("  explorer        Open the file explorer\n", SH_COLOR(COLOR_WHITE));
    vga_write("  clear / cls     Clear the screen\n", SH_COLOR(COLOR_WHITE));
    vga_write("  help            This help\n", SH_COLOR(COLOR_WHITE));
    vga_write("  exit            Power off\n", SH_COLOR(COLOR_WHITE));
}



static void print_entry_name(const FAT_DirectoryEntry* entry) {
    for (int i = 0; i < 8 && entry->Name[i] != ' '; i++) {
        vga_putchar((char)entry->Name[i], SH_COLOR(COLOR_WHITE));
    }
    if (entry->Name[8] != ' ') {
        vga_putchar('.', SH_COLOR(COLOR_WHITE));
        for (int i = 8; i < 11 && entry->Name[i] != ' '; i++) {
            vga_putchar((char)entry->Name[i], SH_COLOR(COLOR_WHITE));
        }
    }
    if (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) {
        vga_write("/", SH_COLOR(COLOR_CYAN));
    }
}

static void list_directory(int detailed, int human) {
    if (!require_fat()) return;

    FAT_File* dir = FAT_Open(&kernel_fat_disk, current_dir);
    if (dir == NULL || !dir->IsDirectory) {
        if (dir != NULL) FAT_Close(dir);
        vga_write("ls: directory not found\n", SH_COLOR(COLOR_RED));
        return;
    }

    FAT_DirectoryEntry entry;
    while (FAT_ReadEntry(&kernel_fat_disk, dir, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID)) continue;

        if (detailed) {
            
            char flags[6];
            flags[0] = (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) ? 'd' : '-';
            flags[1] = (entry.Attributes & 0x01) ? 'r' : '-';
            flags[2] = (entry.Attributes & 0x02) ? 'h' : '-';
            flags[3] = (entry.Attributes & 0x04) ? 's' : '-';
            flags[4] = (entry.Attributes & 0x20) ? 'a' : '-';
            flags[5] = '\0';
            vga_write(flags, SH_COLOR(COLOR_WHITE));
            vga_write("  ", SH_COLOR(COLOR_WHITE));
            if (human && entry.Size >= 1024) {
                vga_print_int((int)(entry.Size / 1024), SH_COLOR(COLOR_WHITE));
                vga_write("K", SH_COLOR(COLOR_WHITE));
            } else {
                vga_print_int((int)entry.Size, SH_COLOR(COLOR_WHITE));
            }
            vga_write("  ", SH_COLOR(COLOR_WHITE));
            print_entry_name(&entry);
            vga_write("\n", SH_COLOR(COLOR_WHITE));
        } else {
            print_entry_name(&entry);
            vga_write("  ", SH_COLOR(COLOR_WHITE));
        }
    }
    if (!detailed) vga_write("\n", SH_COLOR(COLOR_WHITE));
    FAT_Close(dir);
}



static void cmd_cd(const char* arg) {
    if (!require_fat()) return;
    char target[256];
    resolve_path(arg, target);

    FAT_File* dir = FAT_Open(&kernel_fat_disk, target);
    if (dir == NULL || !dir->IsDirectory) {
        if (dir != NULL) FAT_Close(dir);
        vga_write("cd: directory not found\n", SH_COLOR(COLOR_RED));
        return;
    }
    FAT_Close(dir);

    int i = 0;
    while (target[i] && i < 255) {
        current_dir[i] = target[i];
        i++;
    }
    current_dir[i] = '\0';
}

static void cmd_cat(const char* arg) {
    if (!require_fat()) return;
    char path[256];
    resolve_path(arg, path);

    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    if (file == NULL || file->IsDirectory) {
        if (file != NULL) FAT_Close(file);
        vga_write("cat: file not found\n", SH_COLOR(COLOR_RED));
        return;
    }

    uint8_t buffer[128];
    uint32_t bytesRead;
    while ((bytesRead = FAT_Read(&kernel_fat_disk, file, sizeof(buffer), buffer)) > 0) {
        for (uint32_t i = 0; i < bytesRead; i++) {
            if (buffer[i] != '\r') vga_putchar((char)buffer[i], SH_COLOR(COLOR_WHITE));
        }
    }
    FAT_Close(file);
    vga_write("\n", SH_COLOR(COLOR_WHITE));
}

static void cmd_copy(const char* args) {
    if (!require_fat()) return;

    char source_arg[128];
    char destination_arg[128];
    int source_length = 0;
    while (args[source_length] && args[source_length] != ' ' && source_length < (int)sizeof(source_arg) - 1) {
        source_arg[source_length] = args[source_length];
        source_length++;
    }
    source_arg[source_length] = '\0';
    while (args[source_length] == ' ') source_length++;
    if (source_arg[0] == '\0' || args[source_length] == '\0') {
        vga_write("usage: cp <source> <destination>\n", SH_COLOR(COLOR_LIGHT_YELLOW));
        return;
    }

    int destination_length = 0;
    while (args[source_length] && args[source_length] != ' ' &&
           destination_length < (int)sizeof(destination_arg) - 1) {
        destination_arg[destination_length++] = args[source_length++];
    }
    destination_arg[destination_length] = '\0';
    while (args[source_length] == ' ') source_length++;
    if (destination_arg[0] == '\0' || args[source_length] != '\0') {
        vga_write("usage: cp <source> <destination>\n", SH_COLOR(COLOR_LIGHT_YELLOW));
        return;
    }

    uint32_t content_length = 0;
    const uint8_t* memory_data;
    uint32_t memory_size;
    if (kernel_memory_get_file(source_arg, &memory_data, &memory_size)) {
        if (memory_size > sizeof(nano_buffer)) {
            vga_write("cp: kernel_memory source exceeds 2048-byte copy limit\n", SH_COLOR(COLOR_RED));
            return;
        }
        content_length = memory_size;
        for (uint32_t i = 0; i < content_length; i++) nano_buffer[i] = (char)memory_data[i];
    } else {
        char source_path[256];
        resolve_path(source_arg, source_path);
        FAT_File* source = FAT_Open(&kernel_fat_disk, source_path);
        if (source == NULL || source->IsDirectory) {
            if (source != NULL) FAT_Close(source);
            vga_write("cp: source file not found\n", SH_COLOR(COLOR_RED));
            return;
        }
        if (source->Size > sizeof(nano_buffer)) {
            FAT_Close(source);
            vga_write("cp: source exceeds 2048-byte copy limit\n", SH_COLOR(COLOR_RED));
            return;
        }
        const uint32_t source_size = source->Size;
        while (content_length < source_size) {
            const uint32_t count = FAT_Read(&kernel_fat_disk, source,
                                            source_size - content_length,
                                            nano_buffer + content_length);
            if (count == 0) break;
            content_length += count;
        }
        FAT_Close(source);
        if (content_length != source_size) {
            vga_write("cp: could not read complete source\n", SH_COLOR(COLOR_RED));
            return;
        }
    }

    char destination_path[256];
    resolve_path(destination_arg, destination_path);
    FAT_File* destination = FAT_Open(&kernel_fat_disk, destination_path);
    int created = 0;
    if (destination != NULL && destination->IsDirectory) {
        FAT_Close(destination);
        vga_write("cp: destination is a directory\n", SH_COLOR(COLOR_RED));
        return;
    }
    if (destination == NULL) {
        destination = FAT_Create(&kernel_fat_disk, destination_path);
        created = destination != NULL;
    }
    if (destination == NULL) {
        vga_write("cp: cannot create destination\n", SH_COLOR(COLOR_RED));
        return;
    }

    const int saved = FAT_Save(&kernel_fat_disk, destination, nano_buffer, content_length);
    FAT_Close(destination);
    if (!saved) {
        if (created) FAT_Remove(&kernel_fat_disk, destination_path, false);
        vga_write("cp: write failed (destination capacity is 2048 bytes)\n", SH_COLOR(COLOR_RED));
        return;
    }
    vga_write("copied ", SH_COLOR(COLOR_GREEN));
    vga_print_int((int)content_length, SH_COLOR(COLOR_GREEN));
    vga_write(" bytes\n", SH_COLOR(COLOR_GREEN));
}

static void cmd_mkdir(const char* arg) {
    if (!require_fat()) return;
    char path[256];
    resolve_path(arg, path);
    if (FAT_Mkdir(&kernel_fat_disk, path)) {
        vga_write("created\n", SH_COLOR(COLOR_GREEN));
    } else {
        vga_write("mkdir: cannot create directory\n", SH_COLOR(COLOR_RED));
    }
}

static void cmd_remove(const char* arg, int recursive) {
    if (!require_fat()) return;
    char path[256];
    resolve_path(arg, path);
    if (FAT_Remove(&kernel_fat_disk, path, recursive)) {
        vga_write("removed\n", SH_COLOR(COLOR_GREEN));
    } else {
        vga_write("rm: cannot remove path\n", SH_COLOR(COLOR_RED));
    }
}

static void nano_redraw(const char* path, const char* status, uint32_t length) {
    vga_clear();
    vga_write("nano  ", SH_COLOR(COLOR_LIGHT_CYAN));
    vga_write(path, SH_COLOR(COLOR_WHITE));
    vga_write("\nCtrl+S Save   Ctrl+X Save and Exit\n", SH_COLOR(COLOR_LIGHT_YELLOW));
    vga_write(status, SH_COLOR(COLOR_LIGHT_GREEN));
    vga_write("\n", SH_COLOR(COLOR_WHITE));
    for (uint32_t i = 0; i < length; i++) {
        vga_putchar(nano_buffer[i], SH_COLOR(COLOR_WHITE));
    }
}

static void cmd_nano(const char* arg) {
    if (!require_fat()) return;
    char path[256];
    resolve_path(arg, path);

    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    const int isNewFile = file == NULL;
    if (isNewFile) file = FAT_Create(&kernel_fat_disk, path);
    if (file == NULL || file->IsDirectory) {
        if (file != NULL) FAT_Close(file);
        vga_write("nano: cannot open or create FAT file\n", SH_COLOR(COLOR_RED));
        return;
    }

    uint32_t length = 0;
    while (length < sizeof(nano_buffer) - 1) {
        const uint32_t count = FAT_Read(&kernel_fat_disk, file,
                                        sizeof(nano_buffer) - 1 - length,
                                        nano_buffer + length);
        if (count == 0) break;
        length += count;
    }

    const char* status = isNewFile ? "New file" : "";
    nano_redraw(path, status, length);
    while (1) {
        const char key = keyboard_getchar_poll();
        if (key == 19 || key == 24) {
            if (!FAT_Save(&kernel_fat_disk, file, nano_buffer, length)) {
                status = "Save failed: file has limited reserved space";
                nano_redraw(path, status, length);
            } else if (key == 24) {
                FAT_Close(file);
                vga_clear();
                vga_write("Saved.\n", SH_COLOR(COLOR_GREEN));
                return;
            } else {
                status = "Saved";
                nano_redraw(path, status, length);
            }
            continue;
        }

        if (key == '\b') {
            if (length > 0) length--;
        } else if (key == '\n') {
            if (length < sizeof(nano_buffer) - 1) nano_buffer[length++] = '\n';
        } else if (key >= 32 && key <= 126) {
            if (length < sizeof(nano_buffer) - 1) nano_buffer[length++] = key;
        } else {
            continue;
        }
        status = "Modified";
        nano_redraw(path, status, length);
    }
}

static void cmd_explorer(void) {
    explorer_run();
    shell_init();
}



void shell_execute_command(const char* cmd) {
    if (strcmp(cmd, "") == 0) return;
    if (strcmp(cmd, "whoami") == 0) cmd_whoami();
    else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
    else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "fat ls") == 0) list_directory(0, 0);
    else if (strcmp(cmd, "ls -la") == 0) list_directory(1, 0);
    else if (strcmp(cmd, "ls -lh") == 0) list_directory(1, 1);
    else if (strncmp(cmd, "echo ", 5) == 0) cmd_echo(cmd + 5);
    else if (strcmp(cmd, "uname") == 0) cmd_uname();
    else if (strcmp(cmd, "execute") == 0) cmd_execute();
    else if (strcmp(cmd, "explorer") == 0) cmd_explorer();
    else if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "cd") == 0) cmd_cd("/");
    else if (strncmp(cmd, "cd ", 3) == 0) cmd_cd(cmd + 3);
    else if (strncmp(cmd, "cat ", 4) == 0) cmd_cat(cmd + 4);
    else if (strncmp(cmd, "cp ", 3) == 0) cmd_copy(cmd + 3);
    else if (strncmp(cmd, "fat cat ", 8) == 0) cmd_cat(cmd + 8);
    else if (strncmp(cmd, "mkdir ", 6) == 0) cmd_mkdir(cmd + 6);
    else if (strncmp(cmd, "nano ", 5) == 0) cmd_nano(cmd + 5);
    else if (strncmp(cmd, "rm -rf ", 7) == 0) cmd_remove(cmd + 7, 1);
    else if (strncmp(cmd, "rm ", 3) == 0) cmd_remove(cmd + 3, 0);
    else if (strcmp(cmd, "exit") == 0) system_shutdown();
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) vga_clear();
    else if (strcmp(cmd, "test fb") == 0) test_framebuffer();
    else if (strcmp(cmd, "show fb debug info") == 0) {
        vga_write("FB: has=", SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_print_int(fb_info.has_framebuffer, SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_write(" w=", SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_print_int(fb_info.framebuffer_width, SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_write(" h=", SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_print_int(fb_info.framebuffer_height, SH_COLOR(COLOR_LIGHT_YELLOW));
        vga_write("\n", SH_COLOR(COLOR_LIGHT_YELLOW));
    } else {
        vga_write("command not found: ", SH_COLOR(COLOR_RED));
        vga_write(cmd, SH_COLOR(COLOR_RED));
        vga_write("\n", SH_COLOR(COLOR_RED));
    }
}

void shell_run(void) {
    shell_init();
    while (1) {
        shell_prompt();
        shell_read_line(cmd_buffer, CMD_BUFFER_SIZE);
        shell_execute_command(cmd_buffer);
    }
}