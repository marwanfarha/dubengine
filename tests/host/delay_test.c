/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the dub delay. Feedback 100 holds, more swells and stays, less dies; ping pong: left, right, left; a new
 * time Repitch (bent), Fade or Jump (not).
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/delay_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[128];
static float pl, pr;                             /* the loudest left and right (256: full scale) */
static float run_ms(int ms)
{
    int b, i, nb = ms * 44100 / 64000;
    pl = pr = 0.0f;
    for (b = 0; b < nb; b++) {
        en_render(buf, 64, 2048);
        for (i = 0; i < 128; i += 2) {
            const float l = (float)abs(buf[i]) / 32768.0f, r = (float)abs(buf[i + 1]) / 32768.0f;
            pl = l > pl ? l : pl, pr = r > pr ? r : pr;
        }
    }
    return pl > pr ? pl : pr;
}
static void stab(void) { en_key_chord(60, 1), run_ms(80), en_key_chord(60, 0); }
static void fresh(int fb)
{
    int i;
    en_init();
    en_set(P_TEMPO, 120), en_set(P_ROOT, 0), en_set(P_MODE, MODE_AEOLIAN);
    for (i = 0; i < SY_N; i++)
        en_syn_set(i, SYN_INFO[i].def);
    en_syn_set(SY_AA, 0), en_syn_set(SY_AS, 100), en_syn_set(SY_AR, 0), en_syn_set(SY_RMIX, 0), en_syn_set(SY_DWOW, 0);
    en_syn_set(SY_DMIX, 50), en_syn_set(SY_DFB, fb), en_syn_set(SY_DSYNC, 0), en_syn_set(SY_DMS, 60);   /* (~316 ms) */
    run_ms(50);
}

int main(void)
{
    float a, b, c;
    /* Feedback 60: gone in 20 s. 100: still there. 108: there for good, louder, and never past the clip */
    fresh(60), stab(), run_ms(1000), a = run_ms(1000), run_ms(20000), b = run_ms(1000);
    CHECK(a > 0.5f && b < 0.02f * a, "Feedback 60 dies away (%.2f then %.3f)", (double)a, (double)b);
    fresh(100), stab(), run_ms(1000), a = run_ms(1000), run_ms(30000), b = run_ms(1000);
    CHECK(b > 0.25f * a, "Feedback 100 holds (%.2f, 30 s on %.2f)", (double)a, (double)b);
    fresh(108), stab(), run_ms(1000), a = run_ms(1000), run_ms(30000), b = run_ms(1000), run_ms(60000), c = run_ms(1000);
    CHECK(b > a && c > 0.8f * b && c < 256.0f, "Feedback 108 swells and stays (%.2f, %.2f, %.2f)", (double)a, (double)b, (double)c);
    /* ping pong: the first repeat left, the second right, the third left */
    fresh(70), en_syn_set(SY_DPING, 1), run_ms(20), stab();
    run_ms(150);                                 /* (to 230 ms: the dry gone) */
    run_ms(240), a = pl, b = pr;                 /* 230 .. 470: the first repeat (at ~316) */
    CHECK(a > 3.0f * b && a > 1.0f, "ping pong: the first repeat left (%.2f, %.2f)", (double)a, (double)b);
    run_ms(316), a = pl, b = pr;
    CHECK(b > 3.0f * a && b > 0.5f, "the second right (%.2f, %.2f)", (double)a, (double)b);
    run_ms(316), a = pl, b = pr;
    CHECK(a > 3.0f * b && a > 0.3f, "the third left (%.2f, %.2f)", (double)a, (double)b);
    /* without it: every repeat on both sides */
    fresh(70), stab(), run_ms(150), run_ms(240), a = pl, b = pr;
    CHECK(a > 0.5f * b && b > 0.5f * a && a > 1.0f, "no ping pong: both sides (%.2f, %.2f)", (double)a, (double)b);
    /* Jump: the time changed while a repeat rings: the next one on the new time at once */
    fresh(70), en_syn_set(SY_DMODE, EN_DM_JUMP), en_syn_set(SY_DMS, 80), run_ms(20), stab();   /* (~792 ms) */
    run_ms(100), en_syn_set(SY_DMS, 50);         /* (~200 ms: the stab, 180 ms ago, is 20 ms from the head) */
    a = run_ms(100);
    CHECK(a > 1.0f, "Jump: on the new time at once (%.2f)", (double)a);
    fresh(70), en_syn_set(SY_DMODE, EN_DM_FADE), en_syn_set(SY_DMS, 80), run_ms(20), stab();
    run_ms(100), en_syn_set(SY_DMS, 50), run_ms(60);
    a = run_ms(300);
    CHECK(a > 0.5f, "Fade: the new time's repeats, faded in (%.2f)", (double)a);
    printf(fails ? "delay: %d FAILED\n" : "delay: all ok\n", fails);
    return fails != 0;
}
