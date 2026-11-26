// GUESS.EXE：猜數字遊戲（鍵盤輸入、亂數、計時）
#include "../lib/mydos.h"

int main(void) {
    srand((uint32_t)ticks());
    int target = rand() % 100 + 1, tries = 0;
    char line[16];
    uint64_t start = ticks();
    print("I'm thinking of a number from 1 to 100.\n");
    for (;;) {
        print("Your guess: ");
        if (readline(line, sizeof(line)) == 0) continue;
        int g = atoi(line);
        tries++;
        if (g < target)      print("Too low.\n");
        else if (g > target) print("Too high.\n");
        else break;
    }
    uint64_t secs = (ticks() - start) / 1000;
    printf("Correct! %d tries in %d seconds.\n", tries, (int)secs);
    return tries;
}
