// CRASH.EXE：故意做壞事，看 MYDOS 怎麼保護自己
#include "../lib/mydos.h"

int main(int argc, char **argv) {
    char what[16] = "";
    if (argc > 1) {                       // 參數轉大寫，crash kernel 和 CRASH KERNEL 都可以
        int i = 0;
        for (; argv[1][i] && i < 15; i++) what[i] = toupper(argv[1][i]);
        what[i] = 0;
    }
    if (!strcmp(what, "KERNEL")) {
        print("Trying to read kernel memory at 0xFFFFFFFF80000000...\n");
        volatile char *k = (volatile char *)0xFFFFFFFF80000000ULL;
        printf("Got %d (this should never print)\n", *k);
    } else if (!strcmp(what, "PORT")) {
        print("Trying to talk to the keyboard controller directly...\n");
        __asm__ volatile ("outb %0, $0x64" :: "a"((char)0xFE));   // 想偷偷重開機
        print("This should never print\n");
    } else if (!strcmp(what, "LOOP")) {
        print("Spinning forever. Press Ctrl+C to stop me.\n");
        for (volatile int i = 0;; i++) {}
    } else {
        print("Writing to address 0 (null pointer)...\n");
        *(volatile int *)0 = 42;
        print("This should never print\n");
    }
    return 0;
}
