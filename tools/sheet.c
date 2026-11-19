/* Лист поз: строки - персонажи, столбцы - анимации. Использование: sheet <out_prefix>  -> prefix_a.bmp, prefix_b.bmp */
#include "../src/rig.h"
#include <stdio.h>
#include <string.h>
int image_save_bmp(const Image *im, const char *path);
int main(int argc, char **argv) {
    const char *pre = argc > 1 ? argv[1] : "sheet";
    rigs_init();
    const int CW = 64, CH = 84, COLS = 8;
    for (int part = 0; part < 2; part++) {
        Image im = image_create(CW * COLS, CH * RIG_COUNT);
        for (int i = 0; i < im.w * im.h; i++) im.px[i] = 0xFF8A9AB0;
        for (int r = 0; r < RIG_COUNT; r++)
            for (int c = 0; c < COLS; c++) {
                int a = part * COLS + c;
                const Rig *rg = rig_get(r);
                Pose p;
                float t = rg->anims[a].loop ? 0.25f : 0.5f;
                if (a == A_DEATH || a == A_FALLDMG) t = 0.9f;
                rig_pose_at(rg, a, t, &p);
                Image cell = image_create(CW, CH);
                for (int i = 0; i < CW * CH; i++) cell.px[i] = 0xFF8A9AB0;
                model_draw(&cell, rg, &p, a, CW / 2, CH - 6, 1, 0);
                for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) im.px[(r * CH + y) * im.w + c * CW + x] = cell.px[y * CW + x];
                image_free(&cell);
            }
        char path[256];
        snprintf(path, sizeof path, "%s_%c.bmp", pre, 'a' + part);
        image_save_bmp(&im, path);
        image_free(&im);
    }
    return 0;
}
