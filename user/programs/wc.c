// WC.EXE：數一個檔案有幾行、幾個字、幾個 byte（示範讀檔的系統呼叫）
#include "../lib/mydos.h"

int main(int argc, char **argv) {
    if (argc < 2) { print("Usage: WC filename\n"); return 1; }
    int fd = open(argv[1], MYDOS_O_READ);
    if (fd < 0) { printf("Cannot open %s\n", argv[1]); return 1; }
    char buf[512];
    int n, lines = 0, words = 0, bytes = 0, in_word = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') in_word = 0;
            else if (!in_word) { in_word = 1; words++; }
        }
    }
    close(fd);
    printf("%7d lines %7d words %7d bytes  %s\n", lines, words, bytes, argv[1]);
    return 0;
}
