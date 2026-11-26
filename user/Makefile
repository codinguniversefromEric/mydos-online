# 使用者程式：每個 programs/X.c 編成 build/X.EXE（ELF 格式，副檔名借用 DOS 的）
CC := gcc
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic \
          -fno-builtin -m64 -mgeneral-regs-only -nostdlib -static
LDFLAGS := -Wl,-T,user.ld -Wl,--build-id=none -no-pie

SRCS := $(wildcard programs/*.c)
EXES := $(patsubst programs/%.c,build/%.EXE,$(SRCS))
EXES := $(shell echo $(EXES) | tr a-z A-Z | sed 's|BUILD/|build/|g')

all: $(EXES)

build/libc.o: lib/libc.c lib/mydos.h ../src/syscall_nums.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# hello.c → HELLO.EXE：檔名轉大寫才符合 DOS 的 8.3 規則
build/%.EXE: build/libc.o $(wildcard programs/*.c) user.ld
	$(CC) $(CFLAGS) $(LDFLAGS) programs/$(shell echo $* | tr A-Z a-z).c build/libc.o -o $@

clean:
	rm -rf build
