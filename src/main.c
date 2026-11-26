#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "version.h"
#include "serial.h"
#include "term.h"
#include "kprintf.h"
#include "panic.h"
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "apic.h"
#include "irq.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "ata.h"
#include "fs.h"
#include "process.h"

// ---- 向 Limine 要的東西 ----
__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0 };

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0 };

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0 };

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t req_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t req_end[] = LIMINE_REQUESTS_END_MARKER;

static void hcf(void) { for (;;) __asm__ volatile ("cli; hlt"); }

static void ok(const char *what) { kprintf("[ ok ] %s\n", what); }

void kmain(void) {
    serial_init();
    serial_ansi_clear();                          // 清掉 BIOS 留在序列埠終端機上的字
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) hcf();
    if (!fb_req.response || fb_req.response->framebuffer_count < 1) hcf();
    struct limine_framebuffer *fb = fb_req.response->framebuffers[0];
    if (fb->bpp != 32) hcf();

    term_init(fb);
    term_status("MYDOS v" MYDOS_VERSION);
    term_set_color(0xC0C0C0, 0x000000);
    kprintf("Starting MYDOS...\n");

    gdt_init();                                   ok("GDT loaded");
    idt_init();                                   ok("IDT loaded, CPU exceptions handled");

    if (!memmap_req.response || !hhdm_req.response) panic("Limine gave no memory map");
    hhdm_offset = hhdm_req.response->offset;
    pmm_init(memmap_req.response);
    vmm_init();
    heap_init();
    kprintf("[ ok ] Memory: %llu MB free\n", pmm_free_pages() * PAGE_SIZE / (1024 * 1024));

    syscall_init();                               ok("System calls on int 0x80");
    apic_init();
    irq_init();                                   ok("PIC remapped to vectors 32-47");
    timer_init();                                 ok("PIT timer at 100 Hz, RTC clock");
    keyboard_init();
    __asm__ volatile ("sti");                     ok("PS/2 keyboard on IRQ1");

    if (ata_init()) {
        kprintf("[FAIL] No hard disk on the primary IDE channel\n");
    } else {
        kprintf("[ ok ] Disk: %s, %u MB\n", ata_model(), ata_sectors() / 2048);
        int r = fs_mount();
        if (r) kprintf("[FAIL] Drive C: %s\n", fs_strerror(r));
        else   kprintf("[ ok ] Drive C: FAT16 \"%s\" mounted\n", fs_label());
    }
    kprintf("\n");

    shell_run();
}
