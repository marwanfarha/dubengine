/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the Delay and Reverb pages. Mix (0 dry alone, 100 wet alone), the delay's Feedback, the reverb's Decay.
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/fx_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (no MIDI out here) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[128];
static float run_ms(int ms)                      /* the loudest sample */
{
    int b, i, nb = ms * 44100 / 64000;
    float pk = 0.0f;
    for (b = 0; b < nb; b++) {
        en_render(buf, 64, 2048);
        for (i = 0; i < 128; i++) {
            const float v = (float)abs(buf[i]) / 32768.0f;
            if (v > pk)
                pk = v;
        }
    }
    return pk;
}
static void set(int p, int v) { en_syn_set(p, v); }
static void reset(void)
{
    int i;
    for (i = 0; i < SY_N; i++)
        set(i, SYN_INFO[i].def);
    set(SY_AA, 0), set(SY_AS, 100), set(SY_AR, 0);   /* (the chord on and off at once: what's after it, the effects) */
    set(SY_DMIX, 0), set(SY_RMIX, 0);
    run_ms(8000);                                /* (the last test's tails gone) */
}
static float tail(int from_ms, int ms)           /* a chord 300 ms, then the loudest from from_ms after for ms */
{
    en_key_chord(60, 1);
    run_ms(300);
    en_key_chord(60, 0);
    run_ms(from_ms);
    return run_ms(ms);
}

int main(void)
{
    float a, b;
    en_init();
    en_set(P_ROOT, 0);
    en_set(P_MODE, MODE_AEOLIAN);
    en_pool(EN_MODE_POOL[MODE_AEOLIAN]);
    en_set(P_TEMPO, 120);

    /* dry: nothing after the chord */
    reset();
    a = tail(200, 1000);
    CHECK(a < 0.5f, "dry: silent after (%.3f)", (double)a);

    /* the delay: its repeats after the chord; more Feedback, longer */
    reset();
    set(SY_DMIX, 50), set(SY_DFB, 20);
    a = tail(2000, 1000);
    reset();
    set(SY_DMIX, 50), set(SY_DFB, 85);
    b = tail(2000, 1000);
    CHECK(b > 4.0f * a, "Feedback 85 rings on past Feedback 20 (%.3f against %.3f, 2 s after)", (double)b, (double)a);

    /* Mix 100: the dry gone, the chord heard only as its repeats */
    reset();
    set(SY_DMIX, 0);
    en_key_chord(60, 1);
    a = run_ms(60);
    en_key_chord(60, 0);
    reset();
    set(SY_DMIX, 100);
    en_key_chord(60, 1);
    b = run_ms(60);
    en_key_chord(60, 0);
    CHECK(b < 0.1f * a, "Mix 100: no dry (%.3f against %.3f in the first 60 ms)", (double)b, (double)a);

    /* the reverb: a longer Decay, a longer tail */
    reset();
    set(SY_RMIX, 50), set(SY_RDECAY, 0);
    a = tail(1500, 1000);
    reset();
    set(SY_RMIX, 50), set(SY_RDECAY, 100);
    b = tail(1500, 1000);
    CHECK(b > 3.0f * a, "Decay 100 rings on past Decay 0 (%.3f against %.3f, 1.5 s after)", (double)b, (double)a);

    /* Free time: Sync off, ~300 ms; the repeat lands there, not before */
    reset();
    set(SY_DMIX, 100), set(SY_DFB, 0), set(SY_DSYNC, 0), set(SY_DMS, 59);
    run_ms(50);
    CHECK(en_delay_ms(59) > 290.0f && en_delay_ms(59) < 310.0f, "Free time 59 is ~300 ms (%.0f)", (double)en_delay_ms(59));
    en_key_chord(60, 1);
    run_ms(40);
    en_key_chord(60, 0);
    a = run_ms(200);                             /* (40 .. 240 ms: nothing yet) */
    b = run_ms(120);                             /* (240 .. 360 ms: the repeat) */
    CHECK(b > 5.0f * a, "Free: the repeat at ~300 ms (%.3f there, %.3f before)", (double)b, (double)a);

    /* Size: a small room dies sooner than a big one */
    reset();
    set(SY_RMIX, 50), set(SY_RDECAY, 80), set(SY_RSIZE, 0);
    a = tail(1500, 1000);
    reset();
    set(SY_RMIX, 50), set(SY_RDECAY, 80), set(SY_RSIZE, 100);
    b = tail(1500, 1000);
    CHECK(b > 1.5f * a, "Size 100 rings longer than Size 0 (%.3f against %.3f)", (double)b, (double)a);

    /* Freeze: the tail held, long after it would have gone */
    reset();
    set(SY_RMIX, 50), set(SY_RDECAY, 20);
    en_key_chord(60, 1);
    run_ms(300);
    en_key_chord(60, 0);
    run_ms(150);
    set(SY_RFREEZE, 1);
    run_ms(6000);
    a = run_ms(500);
    set(SY_RFREEZE, 0);
    run_ms(6000);
    b = run_ms(500);
    CHECK(a > 0.05f && b < 0.2f * a, "Freeze holds the tail (%.3f after 6 s), let go it dies (%.3f)", (double)a, (double)b);

    if (!fails)
        printf("fx: all ok\n");
    return fails != 0;
}
