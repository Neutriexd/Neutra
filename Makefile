AS = nasm
CC = i686-linux-gnu-gcc
LD = i686-linux-gnu-ld
HOSTCC ?= gcc

CFLAGS = -m32 -nostdlib -nostartfiles -nodefaultlibs -ffreestanding -fno-pie -no-pie -Wall -Wextra
LDFLAGS = -m elf_i386 -T src/linker.ld -nostdlib

QEMU_WIN = "/mnt/c/Program Files/qemu/qemu-system-i386.exe"
BOOT_OBJS = src/boot/header.o src/boot/main.o
KERNEL_OBJS = src/kernel/kernel.o src/kernel/vga.o src/kernel/scheduler.o src/kernel/dce.o src/kernel/idt.o src/kernel/shell.o src/kernel/explorer.o src/kernel/desktop.o src/kernel/graphics.o src/kernel/system.o src/kernel/kernel_memory.o src/kernel/disk.o src/kernel/ata.o src/kernel/fat.o src/kernel/taskbar.o src/kernel/shell_window.o src/kernel/font.o src/kernel/cursor.o

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS)

OUTPUT = kernel.elf
ISO_OUTPUT = os.iso
ISO_STAGING = /tmp/neutra-os-iso
FAT_IMAGE_BUILDER = /tmp/neutra-mkfat12

DISK_IMAGE = disk.img

.PHONY: all clean distclean iso run-iso run-win reset-disk

all: $(OUTPUT)

$(OUTPUT): $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.asm
	$(AS) -f elf32 $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(FAT_IMAGE_BUILDER): tools/mkfat12.c
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror $< -o $@

$(DISK_IMAGE): | $(FAT_IMAGE_BUILDER)
	$(FAT_IMAGE_BUILDER) $@

iso: $(OUTPUT) $(FAT_IMAGE_BUILDER)
	rm -rf $(ISO_STAGING)
	mkdir -p $(ISO_STAGING)/boot/grub $(ISO_STAGING)/boot/kernel_memory
	cp $(OUTPUT) $(ISO_STAGING)/boot/kernel.elf
	cp src/boot/grub/grub.cfg $(ISO_STAGING)/boot/grub/
	: > $(ISO_STAGING)/boot/grub/kernel_memory.cfg
	find src/kernel/kernel_memory -type f -print | sort | while IFS= read -r source; do \
		relative=$${source#src/kernel/kernel_memory/}; \
		target=$(ISO_STAGING)/boot/kernel_memory/$$relative; \
		mkdir -p "$$(dirname "$$target")"; \
		cp "$$source" "$$target"; \
		printf '    module2 /boot/kernel_memory/%s kernel_memory/%s\n' "$$relative" "$$relative" >> $(ISO_STAGING)/boot/grub/kernel_memory.cfg; \
	done
	$(FAT_IMAGE_BUILDER) $(ISO_STAGING)/boot/fat.img
	grub-mkrescue -o $(ISO_OUTPUT) $(ISO_STAGING) 2>/dev/null
	rm -rf $(ISO_STAGING)

run-iso: iso $(DISK_IMAGE)
	qemu-system-i386 -cdrom $(ISO_OUTPUT) \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-boot d -m 256 -vga std -display gtk,grab-on-hover=on

run-win: iso $(DISK_IMAGE)
	$(QEMU_WIN) -cdrom "$$(wslpath -w $(ISO_OUTPUT))" \
		-drive file="$$(wslpath -w $(DISK_IMAGE))",format=raw,if=ide,index=0 \
		-boot d -m 256 -vga std

reset-disk:
	rm -f $(DISK_IMAGE)
	$(MAKE) $(DISK_IMAGE)

clean:
	rm -f $(ALL_OBJS) $(OUTPUT) $(ISO_OUTPUT)
	rm -f $(FAT_IMAGE_BUILDER)
	rm -rf $(ISO_STAGING)

distclean: clean
	rm -f $(DISK_IMAGE)