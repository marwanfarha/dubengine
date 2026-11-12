/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: two tracks, each its own key, pattern and sound; one plate; the bass's and the ping's voices.
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/tracks_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[128];
static float run_ms(int ms)                      /* the loudest sample (256: full scale) */
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
static void sel(int t) { en_sel = (uint8_t)t; }

int main(void)
{
    en_step_t st = {1, 0, 2, EN_VO_ROOT, 2};
    float a, b;
    uint32_t sa, sb;
    en_init();
    en_set(P_TEMPO, 120);
    /* each track its own key: A in C minor, B in D major; the same key pressed, each its own chord */
    sel(0), en_set(P_ROOT, 0), en_set(P_MODE, MODE_AEOLIAN), en_voicing_set(EN_VO_ROOT);
    sel(1), en_set(P_ROOT, 2), en_set(P_MODE, MODE_IONIAN), en_voicing_set(EN_VO_ROOT);
    run_ms(20);
    CHECK(en_trk[0].root == 0 && en_trk[0].mode == MODE_AEOLIAN && en_trk[1].root == 2 && en_trk[1].mode == MODE_IONIAN,
          "each track its key (%d %d, %d %d)", en_trk[0].root, en_trk[0].mode, en_trk[1].root, en_trk[1].mode);
    sel(0), en_key_chord(62, 1);
    sel(1), en_key_chord(62, 1);
    run_ms(50);
    CHECK(en_trk[0].key_tri[1] - en_trk[0].key_tri[0] == 3 && en_trk[0].key_tri[2] - en_trk[0].key_tri[0] == 6,
          "A: D in C minor, diminished (%d %d %d)", en_trk[0].key_tri[0], en_trk[0].key_tri[1], en_trk[0].key_tri[2]);
    CHECK(en_trk[1].key_tri[1] - en_trk[1].key_tri[0] == 4 && en_trk[1].key_tri[2] - en_trk[1].key_tri[0] == 7,
          "B: D in D major, major (%d %d %d)", en_trk[1].key_tri[0], en_trk[1].key_tri[1], en_trk[1].key_tri[2]);
    sel(0), en_key_chord(62, 0);
    sel(1), en_key_chord(62, 0);
    run_ms(3000);
    /* a Locrian triad on the root: diminished */
    sel(0), en_set(P_MODE, MODE_LOCRIAN), en_key_chord(60, 1);
    run_ms(30);
    CHECK(en_trk[0].now_q == EN_Q_DIM, "Locrian's first chord is diminished (%d)", en_trk[0].now_q);
    en_key_chord(60, 0), en_set(P_MODE, MODE_AEOLIAN);
    run_ms(3000);
    /* each its sound: a parameter set on B leaves A's; the plate's own are both tracks', but the send */
    sel(1), en_syn_set(SY_CUT, 90), en_syn_set(SY_RDECAY, 80), en_syn_set(SY_RMIX, 60), en_syn_set(SY_DFB, 20);
    run_ms(20);
    CHECK(en_trk[1].syn[SY_CUT] == 90 && en_trk[0].syn[SY_CUT] == SYN_INFO[SY_CUT].def, "Cutoff: B's alone");
    CHECK(en_trk[1].syn[SY_DFB] == 20 && en_trk[0].syn[SY_DFB] == SYN_INFO[SY_DFB].def, "the delay: B's alone");
    CHECK(en_trk[1].syn[SY_RDECAY] == 80 && en_trk[0].syn[SY_RDECAY] == 80, "the plate's Decay: both tracks'");
    CHECK(en_trk[1].syn[SY_RMIX] == 60 && en_trk[0].syn[SY_RMIX] == SYN_INFO[SY_RMIX].def, "the plate's send: B's alone");
    /* each its pattern, its own length: A 16 steps, B 3; played together, B goes round as A goes once */
    sel(0), en_seq_length(16), en_seq_step(0, &st);
    sel(1), en_seq_length(3), en_seq_step(0, &st);
    en_seq_play(1);
    run_ms(2050);                                /* 16 sixteenths at 120: 2 s */
    sa = en_trk[0].seq_steps, sb = en_trk[1].seq_steps;
    CHECK(sa == sb && sa >= 16 && sa <= 18, "both step together (%u %u)", sa, sb);
    CHECK(en_trk[0].now_n >= 2 && en_trk[1].now_n >= 6, "A's chord twice, B's six times at least (%d %d)",
          en_trk[0].now_n, en_trk[1].now_n);
    en_seq_play(0);
    sel(0), en_seq_clear();
    sel(1), en_seq_clear();
    run_ms(8000);
    /* a track alone: silent A, B playing; the meters tell them apart */
    sel(1), en_syn_set(SY_DMIX, 0), en_syn_set(SY_RMIX, 0);
    en_meter[0] = en_meter[1] = en_meter[2] = 0.0f;
    en_key_chord(62, 1);
    run_ms(300);
    CHECK(en_meter[1] > 0.02f && en_meter[0] < 0.002f && en_meter[2] > 0.02f, "the meters: B alone (%.4f %.4f %.4f)",
          (double)en_meter[0], (double)en_meter[1], (double)en_meter[2]);
    en_key_chord(62, 0);
    run_ms(4000);
    /* the bass and the ping: a note each, heard, then gone */
    a = run_ms(200);
    en_bass_note(36, 100, 300);
    b = run_ms(300);
    CHECK(a < 0.5f && b > 20.0f, "the bass sounds (%.1f then %.1f)", (double)a, (double)b);
    a = run_ms(2000), a = run_ms(200);
    CHECK(a < 0.5f, "and is gone (%.1f)", (double)a);
    en_ping_note(84, 100);
    b = run_ms(100);
    CHECK(b > 10.0f, "the ping sounds (%.1f)", (double)b);
    run_ms(6000);
    a = run_ms(200);
    CHECK(a < 0.5f, "and is gone (%.1f)", (double)a);
    printf(fails ? "tracks: %d FAILED\n" : "tracks: all ok\n", fails);
    return fails != 0;
}
