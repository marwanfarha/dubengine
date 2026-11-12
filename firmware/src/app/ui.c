/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine UI: what the panel (de_ui.c) stands on, and the app's entry points. Part of the unity build (after
 * gfx.c); the host simulator includes it too. */
#include "../dsp/fastmath.h"

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

#include "de_ui.c"                               /* the panel: input, saving, the screen, the lights */

void ui_say(const char *a, const char *b) { de_say2(a, b); }
void ui_init(void)
{
    lcd_fill(0, 0, 240, 240, DE_BG);
    de_init();
}
void ui_frame(void) { de_frame(); }
void ui_input_only(void) { de_input(); }

/* the launch screen, while the FM-1 starts: the name's letters lit one by one, an orange rule drawn under them, then
 * the line and the version. Frame f of UI_SPLASH_N, drawn whole (two bands of 240 x 120) */
static void splash_band(int f, int32_t oy)
{
    static const char NAME[] = "DUBENGINE";
    const int32_t w = text_w(&FONT_T_C, NAME), x0 = 120 - w / 2, y = 92;
    int32_t x = x0;
    int i;
    cv_begin(240, 120, DE_BG);
    for (i = 0; NAME[i]; i++) {
        const char c[2] = {NAME[i], 0};
        const int age = f - 1 - i;
        x = cv_text(x, y - oy, &FONT_T_C, c, age < 0 ? DE_BG : age < 2 ? DE_OR : i < 3 ? DE_OR : DE_FG);
    }
    if (f >= 10) {
        const int32_t rw = f >= 18 ? w : w * (f - 10) / 8;
        cv_rect(x0, y + 30 - oy, rw, 2, DE_OR);
    }
    if (f >= 19) {
        cv_text(120 - text_w(&FONT_T_XS, "CHORD SEQUENCER FOR THE FM1") / 2, y + 44 - oy, &FONT_T_XS,
                "CHORD SEQUENCER FOR THE FM1", mix(DE_BG, DE_DIM, (f - 18) * 4));
        cv_text(120 - text_w(&FONT_T_XS, EN_VERSION) / 2, 220 - oy, &FONT_T_XS, EN_VERSION, DE_LINE);
    }
    cv_blit(0, (uint32_t)oy);
}
void ui_splash(int f)
{
    splash_band(f, 0);
    splash_band(f, 120);
}
