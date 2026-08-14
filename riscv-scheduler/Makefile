CROSS   = riscv64-unknown-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy

ARCH_FLAGS = -march=rv32ima_zicsr_zifencei -mabi=ilp32
CFLAGS  = $(ARCH_FLAGS) -Wall -Wextra -O2 -g -ffreestanding -fno-builtin \
          -fno-stack-protector -nostdlib -nostartfiles -Isrc
LDFLAGS = -T linker/link.ld -nostdlib -Wl,--no-warn-rwx-segments

SRCS_C  = src/uart.c src/sched.c src/mutex.c src/main.c
SRCS_S  = src/start.S
OBJS    = $(SRCS_C:src/%.c=build/%.o) $(SRCS_S:src/%.S=build/%.o)

TARGET  = build/scheduler.elf

.PHONY: all run clean

all: $(TARGET)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

LIBGCC := $(shell $(CC) $(ARCH_FLAGS) -mabi=ilp32 -print-libgcc-file-name)

$(TARGET): $(OBJS) linker/link.ld
	$(CC) $(ARCH_FLAGS) $(LDFLAGS) $(OBJS) $(LIBGCC) -o $@

run: $(TARGET)
	qemu-system-riscv32 -M virt -bios none -nographic -kernel $(TARGET)

clean:
	rm -rf build
