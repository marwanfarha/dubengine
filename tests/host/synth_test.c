/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the chord's synth (de_synth.c). Each wave heard and each its own colour; Fold brightens; the amp's
 * envelope (attack, sustain, release); the filter's envelope; Env 1 and an LFO on their targets.
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/synth_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (no MIDI out here) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[128];
static float pk, bright;                         /* the last run's peak; its brightness: mean |x' | over mean |x| */
static void run_ms(int ms)
{
    int b, i, nb = ms * 44100 / 64000;
    double d = 0.0, a = 0.0;
    float last = 0.0f;
    pk = 0.0f;
    for (b = 0; b < nb; b++) {
        en_render(buf, 64, 2048);
        for (i = 0; i < 128; i += 2) {
            const float v = (float)buf[i] / 32768.0f, av = v < 0.0f ? -v : v;
            if (av > pk)
                pk = av;
            d += (double)(v > last ? v - last : last - v);
            a += (double)av;
            last = v;
        }
    }
    bright = a > 0.0 ? (float)(d / a) : 0.0f;
}
static void reset_sound(void)
{
    int i;
    for (i = 0; i < SY_N; i++)
        en_syn_set(i, SYN_INFO[i].def);
    en_syn_set(SY_DMIX, 0);                      /* (dry: what's measured is the voice) */
    en_syn_set(SY_RMIX, 0);
    run_ms(20);
}
static void hold(int ms) { en_key_chord(60, 1); run_ms(ms); }
static void let_go(void) { en_key_chord(60, 0); run_ms(3000); }

int main(void)
{
    int w;
    float br[EN_NWAVE], p0, p1;
    en_init();
    en_set(P_ROOT, 0);
    en_set(P_MODE, MODE_AEOLIAN);
    en_pool(EN_MODE_POOL[MODE_AEOLIAN]);
    reset_sound();

    /* every wave heard; the sine the darkest, the saw and the square brighter (the filter open) */
    en_syn_set(SY_CUT, 100);
    en_syn_set(SY_FENV, 0);
    for (w = 0; w < EN_NWAVE; w++) {
        en_syn_set(SY_WAVE, w);
        hold(400);
        br[w] = bright;
        CHECK(pk > 0.05f, "%s is heard (%.3f)", EN_WAVE_NAME[w], (double)pk);
        let_go();
    }
    CHECK(br[EN_WAVE_SINE] < br[EN_WAVE_TRI] && br[EN_WAVE_TRI] < br[EN_WAVE_SAW], "sine %.4f < tri %.4f < saw %.4f",
          (double)br[EN_WAVE_SINE], (double)br[EN_WAVE_TRI], (double)br[EN_WAVE_SAW]);
    CHECK(br[EN_WAVE_SQUARE] > br[EN_WAVE_SINE], "square %.4f brighter than sine", (double)br[EN_WAVE_SQUARE]);

    /* Fold: the sine brighter folded */
    en_syn_set(SY_WAVE, EN_WAVE_SINE);
    en_syn_set(SY_FOLD, 100);
    hold(400);
    CHECK(bright > 1.5f * br[EN_WAVE_SINE], "a folded sine is brighter (%.4f against %.4f)", (double)bright,
          (double)br[EN_WAVE_SINE]);
    let_go();
    reset_sound();

    /* the amp: a slow attack quiet at first; sustain 0 dies away held; sustain 100 holds; release rings out */
    en_syn_set(SY_AA, 80);                       /* (~1.6 s) */
    hold(100);
    p0 = pk;
    run_ms(2000);
    CHECK(pk > 4.0f * p0, "a slow attack swells (%.3f then %.3f)", (double)p0, (double)pk);
    let_go();
    en_syn_set(SY_AA, 0);
    en_syn_set(SY_AS, 0);
    en_syn_set(SY_AD, 30);
    hold(100);
    p0 = pk;
    run_ms(1500);
    CHECK(pk < 0.05f * p0, "sustain 0: held, it dies away (%.3f then %.3f)", (double)p0, (double)pk);
    let_go();
    en_syn_set(SY_AS, 100);
    hold(100);
    p0 = pk;
    run_ms(1500);
    CHECK(pk > 0.7f * p0, "sustain 100: held, it holds (%.3f then %.3f)", (double)p0, (double)pk);
    en_key_chord(60, 0);
    en_syn_set(SY_AR, 0);
    run_ms(100);
    run_ms(300);
    CHECK(pk < 0.01f * p0, "release 0: gone at once (%.4f)", (double)pk);
    reset_sound();

    /* the filter's envelope: closed filter, its envelope opens it at the strike */
    en_syn_set(SY_CUT, 20);
    en_syn_set(SY_AS, 100);
    en_syn_set(SY_FENV, 100);
    en_syn_set(SY_FD, 40);
    hold(60);
    p0 = bright;
    run_ms(1500);
    p1 = bright;
    CHECK(p0 > 1.5f * p1, "the filter env: bright at the strike, darker after (%.4f then %.4f)", (double)p0, (double)p1);
    let_go();
    reset_sound();

    /* Env 1 on Pitch, an octave: the strike higher, brighter by the zero crossings' rate */
    en_syn_set(SY_CUT, 100);
    en_syn_set(SY_FENV, 0);
    en_syn_set(SY_WAVE, EN_WAVE_SINE);
    en_syn_set(SY_AS, 100);
    hold(400);
    p1 = bright;
    let_go();
    en_syn_set(SY_1DST, EN_MD_PITCH);
    en_syn_set(SY_1AMT, 100);
    en_syn_set(SY_1A, 0);
    en_syn_set(SY_1S, 100);
    hold(400);
    CHECK(bright > 1.6f * p1, "Env 1 on Pitch up an octave: %.4f against %.4f", (double)bright, (double)p1);
    let_go();
    reset_sound();

    /* LFO 1, a square on Level, slow: loud half the time, quiet the other */
    en_syn_set(SY_AS, 100);
    en_syn_set(SY_L1DST, EN_MD_LEVEL);
    en_syn_set(SY_L1AMT, 100);
    en_syn_set(SY_L1SHAPE, EN_LS_SQUARE);
    en_syn_set(SY_L1RATE, 0);                    /* (0.02 Hz: 25 s each half) */
    hold(300);
    p0 = pk;
    en_syn_set(SY_L1AMT, -100);                  /* (the other way: the other half) */
    run_ms(300);
    run_ms(300);
    CHECK(pk < 0.2f * p0, "LFO 1 on Level: %.3f one way, %.3f the other", (double)p0, (double)pk);
    let_go();

    if (!fails)
        printf("synth: all ok\n");
    return fails != 0;
}
