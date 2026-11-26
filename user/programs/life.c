// LIFE.EXE：康威生命遊戲，全螢幕動畫。按任意鍵結束。
#include "../lib/mydos.h"

#define MAXW 80
#define MAXH 50
static char grid[MAXH][MAXW], next[MAXH][MAXW];

int main(void) {
    int w = screen_cols(), h = screen_rows() - 1;
    if (w > MAXW) w = MAXW;
    if (h > MAXH) h = MAXH;
    srand((uint32_t)ticks());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            grid[y][x] = rand() % 4 == 0;

    color(0x55FF55, 0x000000);
    cls();
    char line[MAXW + 1];
    for (int gen = 0; pollkey() < 0; gen++) {
        for (int y = 0; y < h; y++) {            // 畫出這一代
            for (int x = 0; x < w; x++) line[x] = grid[y][x] ? 'O' : ' ';
            line[w] = 0;
            gotoxy(0, y);
            print(line);
        }
        gotoxy(0, h);
        printf("Generation %d - press any key to quit", gen);
        for (int y = 0; y < h; y++) {            // 算下一代
            for (int x = 0; x < w; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if ((dx || dy) && grid[(y + dy + h) % h][(x + dx + w) % w]) n++;
                next[y][x] = n == 3 || (n == 2 && grid[y][x]);
            }
        }
        memcpy(grid, next, sizeof(grid));
        sleep(100);
    }
    color(0xC0C0C0, 0x000000);
    cls();
    return 0;
}
