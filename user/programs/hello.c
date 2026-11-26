// HELLO.EXE：最小的 MYDOS 程式，印出參數
#include "../lib/mydos.h"

int main(int argc, char **argv) {
    printf("Hello from user mode (ring 3)!\n");
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);
    return 0;
}
