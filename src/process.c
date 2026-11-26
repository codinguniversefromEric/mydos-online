#include "process.h"
#include "gdt.h"
#include "mm.h"
#include "fs.h"
#include "kprintf.h"
#include "string.h"
#include "term.h"

volatile int process_break;
static int running;

// =====================================================================
//  進入與離開 ring 3（這裡是整個 MYDOS 少數非寫組語不可的地方）
//
//  user_enter(entry, rsp, argc, argv)：
//    1. 把核心目前的「被呼叫者保存暫存器」和 rsp 存起來
//    2. 在堆疊上做一個假的中斷返回框架，iretq「返回」到 ring 3
//
//  user_leave(code)：
//    把 rsp 換回步驟 1 存的值、還原暫存器、ret
//    → 看起來就像 user_enter 剛剛回傳了 code
// =====================================================================
// 組語要用名字找到它，所以不能是 static（編譯器會以為沒人用而丟掉）
uint64_t saved_rsp;

int  user_enter(uint64_t entry, uint64_t rsp, uint64_t argc, uint64_t argv);
__attribute__((noreturn)) void user_leave(int code);

__asm__(
    ".global user_enter\n"
    "user_enter:\n"
    "    push %rbx\n"                  // System V 規定這六個暫存器呼叫後要保持原值
    "    push %rbp\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"
    "    mov  %rsp, saved_rsp(%rip)\n"
    "    pushq $0x1B\n"                // SS     = 使用者資料段 | RPL 3
    "    push %rsi\n"                  // RSP    = 使用者堆疊
    "    pushq $0x202\n"               // RFLAGS = IF（開中斷）
    "    pushq $0x23\n"                // CS     = 使用者程式碼段 | RPL 3
    "    push %rdi\n"                  // RIP    = 程式進入點
    "    mov  %rdx, %rdi\n"            // 第一個參數 argc
    "    mov  %rcx, %rsi\n"            // 第二個參數 argv
    "    xor  %eax, %eax\n"            // 其他暫存器清零，不把核心資料留給程式看
    "    xor  %ebx, %ebx\n"
    "    xor  %ecx, %ecx\n"
    "    xor  %edx, %edx\n"
    "    xor  %ebp, %ebp\n"
    "    xor  %r8d, %r8d\n"
    "    xor  %r9d, %r9d\n"
    "    xor  %r10d, %r10d\n"
    "    xor  %r11d, %r11d\n"
    "    xor  %r12d, %r12d\n"
    "    xor  %r13d, %r13d\n"
    "    xor  %r14d, %r14d\n"
    "    xor  %r15d, %r15d\n"
    "    iretq\n"

    ".global user_leave\n"
    "user_leave:\n"
    "    mov  %edi, %eax\n"            // 回傳值 = 結束碼
    "    mov  saved_rsp(%rip), %rsp\n"
    "    pop  %r15\n"
    "    pop  %r14\n"
    "    pop  %r13\n"
    "    pop  %r12\n"
    "    pop  %rbp\n"
    "    pop  %rbx\n"
    "    ret\n"
);

// 程式從 ring 3 進核心（系統呼叫、中斷）時用的堆疊
static uint8_t kernel_stack[32 * 1024] __attribute__((aligned(16)));

// =====================================================================
//  使用者記憶體
// =====================================================================
#define MAX_USER_PAGES 4096                // 最多 16 MB
static uint64_t user_pages[MAX_USER_PAGES];
static int      n_user_pages;

static int map_user_page(uint64_t va) {
    if (vmm_translate(va)) return 0;       // 已經映射過（兩個段落共用一頁）
    if (n_user_pages >= MAX_USER_PAGES) return -1;
    uint64_t p = pmm_alloc_zero();
    if (!p || vmm_map(va, p, PTE_WRITE | PTE_USER)) return -1;
    user_pages[n_user_pages++] = va;
    return 0;
}

static void free_user_memory(void) {
    for (int i = 0; i < n_user_pages; i++) {
        uint64_t p = vmm_unmap(user_pages[i]);
        if (p) pmm_free(p);
    }
    n_user_pages = 0;
}

int user_ptr_ok(const void *ptr, uint64_t len) {
    uint64_t a = (uint64_t)ptr;
    if (a < USER_BASE || a + len < a || a + len > USER_LIMIT) return 0;
    for (uint64_t p = ALIGN_DOWN(a, PAGE_SIZE); p < a + len; p += PAGE_SIZE)
        if (!vmm_translate(p)) return 0;
    return 1;
}

// =====================================================================
//  ELF 載入
// =====================================================================
struct __attribute__((packed)) elf_header {
    uint8_t  ident[16];        // 7F 'E' 'L' 'F'、64 位元、little-endian…
    uint16_t type;             // 2 = 可執行檔
    uint16_t machine;          // 0x3E = x86-64
    uint32_t version;
    uint64_t entry;            // 程式從哪裡開始跑
    uint64_t phoff;            // program header 表在檔案的哪裡
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct __attribute__((packed)) elf_phdr {
    uint32_t type;             // 1 = PT_LOAD：這段要載入記憶體
    uint32_t flags;
    uint64_t offset;           // 在檔案的位置
    uint64_t vaddr;            // 要放在記憶體的位置
    uint64_t paddr;
    uint64_t filesz;           // 檔案裡有幾 byte
    uint64_t memsz;            // 記憶體要佔幾 byte（多的補 0，例如 .bss）
    uint64_t align;
};

static const char *load_elf(const uint8_t *file, uint32_t size, uint64_t *entry) {
    const struct elf_header *h = (const void *)file;
    if (size < sizeof(*h) || memcmp(h->ident, "\x7F" "ELF", 4)) return "Not an executable";
    if (h->ident[4] != 2 || h->machine != 0x3E || h->type != 2) return "Not an x86-64 program";
    if (h->phoff + (uint64_t)h->phnum * sizeof(struct elf_phdr) > size) return "Corrupt program";

    const struct elf_phdr *ph = (const void *)(file + h->phoff);
    for (int i = 0; i < h->phnum; i++) {
        if (ph[i].type != 1) continue;
        uint64_t va = ph[i].vaddr, end = va + ph[i].memsz;
        if (va < USER_BASE || end > USER_STACK_TOP - USER_STACK_SIZE || end < va)
            return "Program wants memory outside its area";
        if (ph[i].offset + ph[i].filesz > size || ph[i].filesz > ph[i].memsz)
            return "Corrupt program";
        for (uint64_t p = ALIGN_DOWN(va, PAGE_SIZE); p < end; p += PAGE_SIZE)
            if (map_user_page(p)) return "Not enough memory";
        // 同一張頁表，核心可以直接寫使用者位址
        memcpy((void *)va, file + ph[i].offset, ph[i].filesz);
        memset((void *)(va + ph[i].filesz), 0, ph[i].memsz - ph[i].filesz);
    }
    if (h->entry < USER_BASE || !vmm_translate(h->entry)) return "Bad entry point";
    *entry = h->entry;
    return 0;
}

// 把參數字串放到使用者堆疊頂端，回傳 argv 的位址
//   高位址 ┌──────────────────┐ USER_STACK_TOP
//          │ "HELLO\0abc\0"   │ 字串本身
//          │ argv[0..n], NULL │ 指標陣列
//   低位址 └──────────────────┘ ← rsp
static uint64_t push_args(const char *prog, const char *args, uint64_t *argc_out) {
    char buf[256];
    ksnprintf(buf, sizeof(buf), "%s %s", prog, args);
    uint64_t sp = USER_STACK_TOP;
    uint64_t len = strlen(buf) + 1;
    sp -= ALIGN_UP(len, 16);
    char *str = (char *)sp;
    memcpy(str, buf, len);

    char *argv_tmp[32];
    int argc = 0;
    for (char *p = str; *p && argc < 31;) {            // 空白切開，原地補 \0
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv_tmp[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv_tmp[argc] = 0;
    sp -= ALIGN_UP((argc + 1) * 8, 16);
    memcpy((void *)sp, argv_tmp, (argc + 1) * 8);
    *argc_out = argc;
    return sp;
}

// =====================================================================
//  執行
// =====================================================================
int process_running(void) { return running; }

int process_run(const char *path, const char *args) {
    void *file;
    uint32_t size;
    int r = fs_read_file(path, &file, &size);
    if (r) { kprintf("%s\n", fs_strerror(r)); return -1; }

    uint64_t entry;
    const char *err = load_elf(file, size, &entry);
    kfree(file);
    for (uint64_t p = USER_STACK_TOP - USER_STACK_SIZE; !err && p < USER_STACK_TOP; p += PAGE_SIZE)
        if (map_user_page(p)) err = "Not enough memory";
    if (err) {
        kprintf("%s\n", err);
        free_user_memory();
        return -1;
    }

    const char *prog = strrchr(path, '\\');
    uint64_t argc, argv = push_args(prog ? prog + 1 : path, args, &argc);
    uint64_t rsp = argv - 8;      // 假裝有人 call 進來：進入點時 rsp+8 要 16 對齊

    tss_set_kernel_stack((uint64_t)kernel_stack + sizeof(kernel_stack));
    process_break = 0;
    running = 1;
    int code = user_enter(entry, rsp, argc, argv);   // ← 程式結束時從這裡「回來」
    running = 0;
    __asm__ volatile ("sti");     // 可能是從中斷或系統呼叫跳回來的，中斷還關著

    free_user_memory();
    return code;
}

void process_exit(int code) {
    user_leave(code);
}

void process_fault(int vec, const char *name, uint64_t err, struct int_frame *f) {
    kprintf("\n%s in program at %p (exception %d", name, (void *)f->rip, vec);
    if (vec == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf(", address %p", (void *)cr2);
    }
    (void)err;
    kprintf(")\nProgram terminated.\n");
    user_leave(-1);
}

void process_check_break(struct int_frame *f) {
    if (process_break && running && (f->cs & 3) == 3) {
        process_break = 0;
        kprintf("^C\n");
        user_leave(-2);
    }
}
