# =====================================================================
#  MYDOS 建置
#    make        編核心、做開機光碟 mydos.iso、做硬碟映像 disk.img
#    make run    在 QEMU 裡開機（序列埠輸出會出現在這個終端機）
#    make clean  清掉所有產物（disk.img 也會重做，裡面新增的檔案會不見）
#
#  需要的套件（Ubuntu / WSL）：
#    sudo apt install build-essential xorriso qemu-system-x86 dosfstools mtools git
# =====================================================================

CC := gcc

# -ffreestanding        沒有作業系統可以依靠（我們就是作業系統）
# -mcmodel=kernel       核心放在最高的 2 GB 位址
# -mno-red-zone         中斷隨時可能發生，不能使用堆疊指標下方的「紅區」
# -mgeneral-regs-only   不用 SSE／浮點暫存器，中斷處理才不必保存它們
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-stack-check -fno-PIC -m64 -march=x86-64 -mno-80387 -mno-mmx \
          -mno-sse -mno-sse2 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
          -Ilimine-protocol/include
LDFLAGS := -nostdlib -static -Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000 -Wl,--build-id=none

SRCS   := $(wildcard src/*.c)
OBJS   := $(patsubst src/%.c,build/%.o,$(SRCS))
KERNEL := build/kernel.elf

LIMINE_BRANCH := v11.x-binary

.PHONY: all run clean user-programs web serve-web

all: mydos.iso disk.img

# ---- 第一次建置時自動下載 Limine（預先編好的二進位檔）和協定標頭 ----
limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=$(LIMINE_BRANCH) --depth=1 limine

limine-protocol:
	git clone https://github.com/limine-bootloader/limine-protocol.git --depth=1 limine-protocol

# ---- 核心 ----
build/%.o: src/%.c | limine-protocol
	@mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

# ---- 開機光碟：Limine + 核心 ----
mydos.iso: $(KERNEL) limine.conf | limine
	rm -rf iso
	mkdir -p iso/boot/limine iso/EFI/BOOT
	cp $(KERNEL) iso/boot/kernel.elf
	cp limine.conf iso/boot/limine/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso/boot/limine/
	cp limine/BOOTX64.EFI iso/EFI/BOOT/
	xorriso -as mkisofs -R -J -b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label iso -o $@ 2>/dev/null

# ---- 使用者程式（user/ 底下自己有一個 Makefile）----
user-programs:
	$(MAKE) -C user

# ---- 硬碟映像：32 MB FAT16，放 disk/ 的內容和 \BIN 裡的程式 ----
# mtools 遇到「已存在」會停下來問問題（在終端機裡看不到就像當掉），
# 所以 BIN 先檢查再建，並用 </dev/null 讓它不能等輸入
disk.img: user-programs $(shell find disk -type f 2>/dev/null)
	rm -f $@
	mkfs.fat -F 16 -n MYDOS -C $@ 32768 >/dev/null
	mcopy -s -i $@ disk/* ::/
	mdir -i $@ ::/BIN >/dev/null 2>&1 || mmd -i $@ ::/BIN </dev/null
	mcopy -o -i $@ user/build/*.EXE ::/BIN/ </dev/null

# -rtc base=localtime：讓 RTC 顯示本地時間（台灣時間），不是 UTC
run: mydos.iso disk.img
	qemu-system-x86_64 -cdrom mydos.iso -boot d \
		-drive file=disk.img,format=raw,if=ide,index=0 \
		-m 256M -serial stdio -rtc base=localtime -no-reboot

# ---- 網頁版：瀏覽器裡用 QEMU Wasm 開機（可以放上 GitHub Pages）----
# 映像先壓縮：32 MB 的 disk.img 大部分是 0，gzip 後只剩幾百 KB，網頁載入快很多
web: mydos.iso disk.img
	bash tools/fetch-qemu-wasm.sh
	gzip -9 -c mydos.iso > web/mydos.iso.gz
	gzip -9 -c disk.img > web/disk.img.gz

# 本機預覽：打開 http://localhost:8000
serve-web: web
	cd web && python3 -m http.server 8000

clean:
	rm -rf build iso mydos.iso disk.img web/mydos.iso.gz web/disk.img.gz
	$(MAKE) -C user clean

-include $(OBJS:.o=.d)
