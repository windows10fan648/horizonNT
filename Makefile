CC      := gcc
LD      := ld
OBJCOPY := objcopy
QEMU    := qemu-system-x86_64

BUILD   := build

KCFLAGS := -std=gnu11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -Werror -O2 -Iinclude

KASFLAGS := -m64 -Iinclude

UCFLAGS := -std=gnu11 -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mno-red-zone -Wall -Wextra -Werror -O2

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

.PHONY: all clean run run-nographic test

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

$(BUILD)/init.elf: $(U_OBJS) user/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) $(U_OBJS) -o $@

$(BUILD)/spawnprobe.elf: $(SPAWNPROBE_OBJS) user/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) $(SPAWNPROBE_OBJS) -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(KASFLAGS) -c $< -o $@

$(BUILD)/kernel/exec/embedded_user.o: $(BUILD)/init.elf $(BUILD)/spawnprobe.elf

$(BUILD)/horizonnt64.elf: $(K_OBJS) linker.ld
	$(LD) $(KLDFLAGS) $(K_OBJS) -o $@

$(BUILD)/horizonnt.elf: $(BUILD)/horizonnt64.elf
	$(OBJCOPY) -O elf32-i386 $< $@

test: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -append autotest -m 256M -nographic -no-reboot

run: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -m 256M -serial stdio -no-reboot

run-nographic: all
	$(QEMU) -kernel $(BUILD)/horizonnt.elf -m 256M -nographic -no-reboot

clean:
	rm -rf $(BUILD)
