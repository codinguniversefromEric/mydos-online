#include "process.h"
#include "syscall_nums.h"
#include "idt.h"
#include "keyboard.h"
#include "kprintf.h"
#include "term.h"
#include "timer.h"
#include "fs.h"
#include "shell.h"
#include "mm.h"
#include "string.h"

// =====================================================================
//  系統呼叫：程式執行 int 0x80，CPU 切到 ring 0 跳進 syscall_entry。
//  約定（跟 Linux 很像）：rax = 編號，rdi / rsi / rdx = 參數，rax = 回傳值。
//
//  這裡需要讀寫「程式當下的暫存器」，__attribute__((interrupt)) 做不到，
//  所以入口是一小段組語：把暫存器全部推上堆疊，當成 struct regs 交給 C。
// =====================================================================
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    struct int_frame frame;            // CPU 自己推的 rip cs rflags rsp ss
};

void syscall_dispatch(struct regs *r);

__asm__(
    ".global syscall_entry\n"
    "syscall_entry:\n"
    "    push %rax\n"   "    push %rbx\n"  "    push %rcx\n"  "    push %rdx\n"
    "    push %rsi\n"   "    push %rdi\n"  "    push %rbp\n"  "    push %r8\n"
    "    push %r9\n"    "    push %r10\n"  "    push %r11\n"  "    push %r12\n"
    "    push %r13\n"   "    push %r14\n"  "    push %r15\n"
    "    mov  %rsp, %rdi\n"            // 參數 = 指向剛剛推上去的 struct regs
    "    call syscall_dispatch\n"
    "    pop %r15\n"    "    pop %r14\n"   "    pop %r13\n"   "    pop %r12\n"
    "    pop %r11\n"    "    pop %r10\n"   "    pop %r9\n"    "    pop %r8\n"
    "    pop %rbp\n"    "    pop %rdi\n"   "    pop %rsi\n"   "    pop %rdx\n"
    "    pop %rcx\n"    "    pop %rbx\n"   "    pop %rax\n"
    "    iretq\n"
);
void syscall_entry(void);

void syscall_init(void) {
    idt_set_gate_ex(0x80, syscall_entry, 3, 0);    // DPL 3：ring 3 才能 int 0x80
}

// ---------------------------------------------------------------------
//  檔案代號（file descriptor）：程式開檔時整個讀進核心，關檔時才寫回
// ---------------------------------------------------------------------
#define MAX_FILES 8
struct file {
    int      used, writing;
    char     path[FS_MAX_PATH];
    uint8_t *data;
    uint32_t size, cap, pos;
};
static struct file files[MAX_FILES];

static int64_t sys_open(const char *upath, int mode) {
    if (!user_ptr_ok(upath, 1)) return -1;
    char path[FS_MAX_PATH];
    strncpy0(path, upath, sizeof(path));
    char full[FS_MAX_PATH];
    if (path_resolve(path, full)) return FS_EINVAL;

    int fd = 0;
    while (fd < MAX_FILES && files[fd].used) fd++;
    if (fd == MAX_FILES) return -1;
    struct file *f = &files[fd];
    memset(f, 0, sizeof(*f));
    strcpy(f->path, full);
    if (mode == MYDOS_O_WRITE) {
        f->writing = 1;
        f->cap = 4096;
        f->data = kmalloc(f->cap);
        if (!f->data) return FS_ENOSPC;
    } else {
        int r = fs_read_file(full, (void **)&f->data, &f->size);
        if (r) return r;
    }
    f->used = 1;
    return fd + 3;                    // 0、1、2 保留給鍵盤和螢幕（習慣）
}

static struct file *get_file(int64_t fd) {
    fd -= 3;
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return 0;
    return &files[fd];
}

static int64_t sys_read(int64_t fd, uint8_t *buf, uint64_t len) {
    struct file *f = get_file(fd);
    if (!f || f->writing || !user_ptr_ok(buf, len)) return -1;
    uint64_t n = f->size - f->pos;
    if (n > len) n = len;
    memcpy(buf, f->data + f->pos, n);
    f->pos += n;
    return n;
}

static int64_t sys_fwrite(int64_t fd, const uint8_t *buf, uint64_t len) {
    struct file *f = get_file(fd);
    if (!f || !f->writing || !user_ptr_ok(buf, len)) return -1;
    while (f->size + len > f->cap) {
        uint8_t *bigger = kmalloc(f->cap * 2);
        if (!bigger) return FS_ENOSPC;
        memcpy(bigger, f->data, f->size);
        kfree(f->data);
        f->data = bigger;
        f->cap *= 2;
    }
    memcpy(f->data + f->size, buf, len);
    f->size += len;
    return len;
}

static int64_t sys_close(int64_t fd) {
    struct file *f = get_file(fd);
    if (!f) return -1;
    int r = 0;
    if (f->writing) r = fs_write_file(f->path, f->data, f->size);
    kfree(f->data);
    f->used = 0;
    return r;
}

static void close_all(void) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].used) sys_close(i + 3);
}

// ---------------------------------------------------------------------
static int64_t sys_readline(char *buf, uint64_t max) {
    if (!max || !user_ptr_ok(buf, max)) return -1;
    uint64_t len = 0;
    for (;;) {
        int c = keyboard_getc();
        if (c == 3 && process_break) break;        // Ctrl+C：馬上返回，讓 dispatch 結束程式
        if (c == '\n') { term_putc('\n'); break; }
        if (c == '\b') { if (len) { len--; term_putc('\b'); } continue; }
        if (c >= ' ' && c <= '~' && len + 1 < max) { buf[len++] = (char)c; term_putc((char)c); }
    }
    buf[len] = 0;
    return len;
}

void syscall_dispatch(struct regs *r) {
    uint64_t a = r->rdi, b = r->rsi, c = r->rdx;
    int64_t ret = 0;
    switch (r->rax) {
    case SYS_EXIT:
        close_all();
        process_exit((int)a);
    case SYS_PUTS:
        if (!user_ptr_ok((void *)a, b)) { ret = -1; break; }
        for (uint64_t i = 0; i < b; i++) term_putc(((char *)a)[i]);
        ret = b;
        break;
    case SYS_GETKEY:   ret = keyboard_getc(); break;
    case SYS_POLLKEY:  ret = keyboard_poll(); break;
    case SYS_READLINE: ret = sys_readline((char *)a, b); break;
    case SYS_OPEN:     ret = sys_open((const char *)a, (int)b); break;
    case SYS_READ:     ret = sys_read(a, (uint8_t *)b, c); break;
    case SYS_WRITE:    ret = sys_fwrite(a, (const uint8_t *)b, c); break;
    case SYS_CLOSE:    ret = sys_close(a); break;
    case SYS_DELETE: {
        char full[FS_MAX_PATH];
        if (!user_ptr_ok((void *)a, 1) || path_resolve((const char *)a, full)) { ret = -1; break; }
        ret = fs_delete(full);
        break;
    }
    case SYS_TICKS:    ret = timer_ticks() * (1000 / TIMER_HZ); break;
    case SYS_SLEEP:    timer_sleep_ms(a); break;
    case SYS_TIME:
        if (!user_ptr_ok((void *)a, sizeof(struct datetime))) { ret = -1; break; }
        rtc_read((struct datetime *)a);
        break;
    case SYS_CLS:      term_clear(); break;
    case SYS_COLOR:    term_set_color((uint32_t)a, (uint32_t)b); break;
    case SYS_SCREEN:   ret = ((int64_t)term_cols() << 32) | (uint32_t)term_rows(); break;
    case SYS_GOTO:     term_goto((int)a, (int)b); break;
    default:           ret = -1;
    }
    r->rax = ret;
    process_check_break(&r->frame);   // 系統呼叫期間按了 Ctrl+C
}
