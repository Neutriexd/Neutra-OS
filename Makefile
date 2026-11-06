
AS = nasm
CC = i686-linux-gnu-gcc
LD = i686-linux-gnu-ld
HOSTCC ?= gcc

CFLAGS = -m32 -nostdlib -nostartfiles -nodefaultlibs -ffreestanding -fno-pie -no-pie -Wall -Wextra \
         -Isrc/kernel -Isrc/desktop -Isrc/programs -Isrc/sound -Isrc/usb -Isrc/net

LDFLAGS = -m elf_i386 -T src/linker.ld -nostdlib

QEMU_WIN = "/mnt/c/Program Files/qemu/qemu-system-i386.exe"

BOOT_OBJS = \
	src/boot/header.o \
	src/boot/main.o


KERNEL_OBJS = \
	src/kernel/kernel.o \
	src/kernel/boot.o \
	src/kernel/vga.o \
	src/kernel/scheduler.o \
	src/kernel/idt.o \
	src/kernel/idt_asm.o \
	src/kernel/shell.o \
	src/kernel/graphics.o \
	src/kernel/system.o \
	src/kernel/kernel_memory.o \
	src/kernel/disk.o \
	src/kernel/ata.o \
	src/kernel/fat.o \
	src/kernel/shortname.o \
	src/kernel/font.o \
	src/kernel/cursor.o \
	src/kernel/window.o \
	src/kernel/bmp.o \
	src/desktop/desktop.o \
	src/desktop/menu.o \
	src/desktop/taskbar.o \
	src/programs/cde.o \
	src/programs/developer_.o \
	src/programs/editor.o \
	src/programs/explorer.o \
	src/programs/program.o \
	src/programs/shell_window.o \
	src/sound/sound.o \
	src/sound/sound_asm.o \
	src/usb/usb.o \
	src/programs/browser.o \
	src/net/pci.o \
	src/net/rtl8139.o \
	src/net/net.o \
	src/net/net_proto.o src/net/dns.o src/net/tcp.o src/net/http.o src/net/inflate.o src/net/inflate.o \
	src/net/net.o 

MBED_OBJS = $(patsubst %.c,%.o,$(wildcard src/net/mbedtls/library/*.c)) src/net/tls.o src/net/entropy.o src/net/libc_min.o
CFLAGS += -Isrc/net/mbedtls/include -Isrc/net/mbedtls/library -Isrc/net/mbedtls/config -DMBEDTLS_CONFIG_FILE='"neutra_mbedtls_config.h"' -Isrc/net/mbedtls/config -Isrc/net/mbedtls/include -Isrc/net/mbedtls/library -fno-stack-protector -ffunction-sections -fdata-sections

JS_OBJS = $(patsubst %.c,%.o,$(wildcard src/js/quickjs/*.c)) src/js/js_host.o src/js/libc_js.o

CFLAGS += -Isrc/js -Isrc/js/quickjs

CSS_OBJS = src/css/css.o

CFLAGS += -Isrc/css

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(MBED_OBJS) $(JS_OBJS) $(DOM_OBJS) $(CSS_OBJS) src/cookie/cookie.o

COOKIE_OBJS = src/cookie/cookie.o

CFLAGS += -Isrc/cookie

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(MBED_OBJS) $(JS_OBJS) $(DOM_OBJS) $(CSS_OBJS) $(IMG_OBJS) $(COOKIE_OBJS) src/cookie/cookie.o


ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(MBED_OBJS) $(JS_OBJS) $(DOM_OBJS) $(CSS_OBJS) src/cookie/cookie.o

src/sound/sound.o: src/sound/sound.c src/sound/sound.h
	$(CC) $(CFLAGS) -c $< -o $@

src/sound/sound_asm.o: src/sound/sound.asm
	$(AS) -f elf32 $< -o $@

src/usb/usb.o: src/usb/usb.c
	$(CC) $(CFLAGS) -c $< -o $@

OUTPUT = kernel.elf
ISO_OUTPUT = os.iso
ISO_STAGING = /tmp/neutra-os-iso
FAT_IMAGE_BUILDER = /tmp/neutra-mkfat12
DISK_IMAGE = disk.img

.PHONY: all clean distclean iso run-iso run-win reset-disk

all: $(OUTPUT)

DOM_OBJS = src/dom/dom.o src/dom/layout.o src/js/js_page.o
CFLAGS += -Isrc/dom -Isrc/js -Isrc/js/quickjs
IMG_OBJS = src/img/png.o

CFLAGS += -Isrc/img

ALL_OBJS = $(BOOT_OBJS) $(KERNEL_OBJS) $(MBED_OBJS) $(JS_OBJS) $(DOM_OBJS) $(CSS_OBJS) $(IMG_OBJS) src/cookie/cookie.o

$(OUTPUT): $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(shell $(CC) -m32 -print-libgcc-file-name)

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
	qemu-system-i386 -cpu max -cdrom $(ISO_OUTPUT) \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-device piix3-usb-uhci \
		-boot d -m 256 -vga std -display gtk,grab-on-hover=on

run-win: iso $(DISK_IMAGE)
	$(QEMU_WIN) -cpu max -accel tcg,thread=single -cdrom "$$(wslpath -w $(ISO_OUTPUT))" \
		-drive file="$$(wslpath -w $(DISK_IMAGE))",format=raw,if=ide,index=0 \
		-device piix3-usb-uhci \
		-boot d -m 256 -vga std \
		-audiodev dsound,id=audio0 \
		-machine pcspk-audiodev=audio0 \
		-device rtl8139,netdev=n0 -netdev user,id=n0 \
		-no-reboot -d int,cpu_reset -D qemu.log \
		-display gtk,zoom-to-fit=on

reset-disk:
	rm -f $(DISK_IMAGE)
	$(MAKE) $(DISK_IMAGE)

clean:
	rm -f $(ALL_OBJS) $(OUTPUT) $(ISO_OUTPUT)
	rm -f $(FAT_IMAGE_BUILDER)
	rm -f src/kernel/explorer.o src/kernel/desktop.o src/kernel/taskbar.o
	rm -f src/kernel/menu.o src/kernel/editor.o src/kernel/program.o
	rm -f src/kernel/developer_.o src/kernel/sound.o src/kernel/sound_asm.o
	rm -rf $(ISO_STAGING)

distclean: clean
	rm -f $(DISK_IMAGE)

