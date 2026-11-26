// UPPER.EXE：把檔案轉成大寫存成新檔（示範寫檔的系統呼叫）
#include "../lib/mydos.h"

int main(int argc, char **argv) {
    if (argc < 3) { print("Usage: UPPER source target\n"); return 1; }
    int in = open(argv[1], MYDOS_O_READ);
    if (in < 0) { printf("Cannot open %s\n", argv[1]); return 1; }
    int out = open(argv[2], MYDOS_O_WRITE);
    if (out < 0) { printf("Cannot create %s\n", argv[2]); close(in); return 1; }
    char buf[256];
    int n, total = 0;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) buf[i] = toupper(buf[i]);
        write(out, buf, n);
        total += n;
    }
    close(in);
    if (close(out) < 0) { print("Write failed\n"); return 1; }   // 關檔時才真正存檔
    printf("%d bytes written to %s\n", total, argv[2]);
    return 0;
}
