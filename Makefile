CC           := gcc
LD           := ld
AR           := ar
OBJCOPY      := objcopy
QEMU         := qemu-system-x86_64
GRUB_MKIMAGE ?= grub-mkimage
GRUB_DIR     ?= /usr/lib/grub/i386-pc
XORRISO      ?= xorriso

BUILD    := build
LIBH_DIR := lib/libhorizon
LIBH_INC := $(LIBH_DIR)/include

ISO       := $(BUILD)/horizonnt.iso
ISO_DIR   := $(BUILD)/iso
GRUB_IMG  := $(ISO_DIR)/boot/grub/eltorito.img
GRUB_MBR  := $(GRUB_DIR)/boot_hybrid.img
GRUB_CFG  := boot/grub.cfg
GRUB_MODULES := biosdisk iso9660 part_msdos normal configfile search echo test \
                serial terminal multiboot

KCFLAGS := -std=gnu11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -Werror -O2 -Iinclude -I$(LIBH_INC)

KASFLAGS := -m64 -Iinclude -I$(LIBH_INC)

UCFLAGS := -std=gnu11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -Werror -O2 -I$(LIBH_INC)

KLDFLAGS := -T linker.ld -z max-page-size=0x1000 -nostdlib --no-warn-rwx-segments
ULDFLAGS := -T user/user.ld -z max-page-size=0x1000 -nostdlib --no-warn-rwx-segments

K_C_SRCS := \
	arch/x86_64/gdt.c \
	arch/x86_64/idt.c \
	arch/x86_64/hal.c \
	kernel/kmain.c \
	kernel/console.c \
	kernel/string.c \
	kernel/mm/pmm.c \
	kernel/mm/vmm.c \
	kernel/mm/heap.c \
	kernel/ob/object.c \
	kernel/ps/process.c \
	kernel/ps/sched.c \
	kernel/io/iomgr.c \
	kernel/io/devices.c \
	kernel/fs/vfs.c \
	kernel/exec/elf.c \
	kernel/exec/syscall.c \
	kernel/shell/shell.c

K_S_SRCS := \
	arch/x86_64/boot.S \
	arch/x86_64/isr.S \
	arch/x86_64/syscall_asm.S \
	kernel/exec/embedded_user.S

K_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(K_C_SRCS)) $(patsubst %.S,$(BUILD)/%.o,$(K_S_SRCS))

U_COMMON_OBJS := $(BUILD)/user/crt0.o
U_OBJS := $(U_COMMON_OBJS) $(BUILD)/user/init.o
SPAWNPROBE_OBJS := $(U_COMMON_OBJS) $(BUILD)/user/spawnprobe.o
LIBTEST_OBJS := $(U_COMMON_OBJS) $(BUILD)/user/libtest.o

LIBH_C_SRCS := \
	$(LIBH_DIR)/runtime.c \
	$(LIBH_DIR)/io.c \
	$(LIBH_DIR)/string.c \
	$(LIBH_DIR)/stdio.c
LIBH_S_SRCS := $(LIBH_DIR)/syscall_asm.S
LIBH_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIBH_C_SRCS)) \
             $(patsubst %.S,$(BUILD)/%.o,$(LIBH_S_SRCS))
LIBH := $(BUILD)/libhorizon.a

.PHONY: all clean run run-nographic test iso

all: $(BUILD)/horizonnt.elf

$(BUILD)/user/crt0.o: user/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/user/init.o: user/init.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/user/spawnprobe.o: user/spawnprobe.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/user/libtest.o: user/libtest.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

# libHorizon is user code: never compile it with the kernel flags.
$(BUILD)/lib/libhorizon/%.o: lib/libhorizon/%.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/lib/libhorizon/%.o: lib/libhorizon/%.S
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(LIBH): $(LIBH_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD)/init.elf: $(U_OBJS) $(LIBH) user/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) $(U_OBJS) --start-group $(LIBH) --end-group -o $@

$(BUILD)/spawnprobe.elf: $(SPAWNPROBE_OBJS) $(LIBH) user/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) $(SPAWNPROBE_OBJS) --start-group $(LIBH) --end-group -o $@

$(BUILD)/libtest.elf: $(LIBTEST_OBJS) $(LIBH) user/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) $(LIBTEST_OBJS) --start-group $(LIBH) --end-group -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KASFLAGS) -c $< -o $@

$(BUILD)/kernel/exec/embedded_user.o: $(BUILD)/init.elf $(BUILD)/spawnprobe.elf $(BUILD)/libtest.elf

$(BUILD)/horizonnt64.elf: $(K_OBJS) linker.ld
	$(LD) $(KLDFLAGS) $(K_OBJS) -o $@

$(BUILD)/horizonnt.elf: $(BUILD)/horizonnt64.elf
	$(OBJCOPY) -O elf32-i386 $< $@

# Bootable BIOS ISO: an El Torito GRUB core image plus the Multiboot kernel.
$(GRUB_IMG):
	@if [ ! -f "$(GRUB_DIR)/cdboot.img" ]; then \
	    echo "error: GRUB i386-pc modules not found in $(GRUB_DIR)" >&2; \
	    echo "       install grub-pc-bin or set GRUB_DIR=/path/to/grub/i386-pc" >&2; \
	    exit 1; \
	fi
	@mkdir -p $(dir $@)
	$(GRUB_MKIMAGE) -O i386-pc-eltorito -o $@ -p /boot/grub $(GRUB_MODULES)

$(ISO): $(BUILD)/horizonnt.elf $(GRUB_CFG) $(GRUB_IMG)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD)/horizonnt.elf $(ISO_DIR)/boot/horizonnt.elf
	cp $(GRUB_CFG) $(ISO_DIR)/boot/grub/grub.cfg
	$(XORRISO) -as mkisofs \
	    -iso-level 3 -full-iso9660-filenames -volid HORIZONNT \
	    -o $@ \
	    -b boot/grub/eltorito.img -no-emul-boot \
	    -boot-load-size 4 -boot-info-table \
	    --grub2-boot-info \
	    $(if $(wildcard $(GRUB_MBR)),--grub2-mbr $(GRUB_MBR),) \
	    $(ISO_DIR)

iso: $(ISO)
	@echo "Bootable ISO: $(ISO)"

test: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -append autotest -m 256M -nographic -no-reboot

run: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -m 256M -serial stdio -no-reboot

run-nographic: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -m 256M -nographic -no-reboot

clean:
	rm -rf $(BUILD)
