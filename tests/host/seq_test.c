/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the sequencer. Steps at the tempo and the step value, their chords, live recording on the nearest step as
 * long as held (replacing), the metronome, clear and bring back.
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/seq_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (no MIDI out here) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[128];
static float run_ms(int ms)                      /* render ms of audio in 64-frame blocks; the loudest sample */
{
    int b, i, nb = ms * 44100 / 64000;
    float pk = 0.0f;
    for (b = 0; b < nb; b++) {
        en_render(buf, 64, 2048);
        for (i = 0; i < 128; i++) {
            float v = (float)abs(buf[i]) / 32768.0f;
            if (v > pk)
                pk = v;
        }
    }
    return pk;
}
static void step(int i, int on, int deg, int oct, int len)
{
    en_step_t t;
    t.on = (uint8_t)on, t.deg = (uint8_t)deg, t.oct = (uint8_t)oct, t.voi = EN_VO_KEY, t.len = (uint8_t)len;
    en_seq_step(i, &t);
}

int main(void)
{
    int i;
    en_init();
    en_set(P_ROOT, 0);
    en_set(P_MODE, MODE_AEOLIAN);
    en_pool(EN_MODE_POOL[MODE_AEOLIAN]);
    en_set(P_TEMPO, 120);
    en_seq_length(16);
    en_seq_division(EN_DIV_16);
    run_ms(50);

    /* a note and its step: D4 in C minor is degree 1 in the root's octave (stored 2); Bb3 degree 6 an octave down */
    {
        en_step_t t;
        CHECK(en_note_step(62, &t) && t.deg == 1 && t.oct == 2, "D4: degree %d octave %d", t.deg, t.oct);
        CHECK(en_note_step(58, &t) && t.deg == 6 && t.oct == 1, "Bb3: degree %d octave %d", t.deg, t.oct);
        CHECK(!en_note_step(64, &t), "E4 is not in C minor");
        t.deg = 4, t.oct = 2;
        CHECK(en_seq_note(&t) == 67, "degree 4 in the root's octave is G4 (%d)", en_seq_note(&t));
    }

    /* the steps keep time: 120 BPM in sixteenths is 8 a second; in eighths 4 */
    en_seq_play(1);
    run_ms(2000);
    CHECK(en_seq_steps >= 15 && en_seq_steps <= 17, "sixteenths at 120: %u steps in 2 s", (unsigned)en_seq_steps);
    en_seq_play(0);
    en_seq_division(EN_DIV_8);
    en_seq_play(1);
    run_ms(2000);
    CHECK(en_seq_steps >= 7 && en_seq_steps <= 9, "eighths at 120: %u steps in 2 s", (unsigned)en_seq_steps);
    en_seq_play(0);
    en_seq_division(EN_DIV_16);
    run_ms(3000);

    /* an empty pattern, the metronome off: silent; on: clicks */
    en_metro(0, 60);
    en_seq_play(1);
    CHECK(run_ms(1000) < 0.002f, "an empty pattern, no metronome: silent");
    en_metro(1, 60);
    CHECK(run_ms(1000) > 0.05f, "the metronome clicks");
    en_metro(0, 60);
    en_seq_play(0);
    run_ms(500);

    /* a pattern plays its chords: step 0 Cm, step 8 Gm (degree 4) */
    step(0, 1, 0, 2, 4);
    step(8, 1, 4, 2, 4);
    en_seq_play(1);
    {
        float a = run_ms(300);
        CHECK(a > 0.05f, "step 1's chord is heard (%.3f)", (double)a);
        CHECK(en_key_tri[2] == 60, "step 1: Cm, its top note C4 in first inversion (%d)", en_key_tri[2]);
        run_ms(900);                             /* (step 9 is at 1 s) */
        CHECK(en_key_tri[0] == 58 && en_key_tri[1] == 62 && en_key_tri[2] == 67,
              "step 9: Gm in first inversion, Bb3 D4 G4 (%d %d %d)", en_key_tri[0], en_key_tri[1], en_key_tri[2]);
    }
    en_seq_play(0);
    run_ms(500);

    /* live recording: a key just before step 5 lands on step 5; held three steps, it lasts three, and replaces */
    en_seq_clear();
    step(5, 1, 2, 2, 1);
    step(6, 1, 3, 2, 1);
    en_seq_record(1);
    en_seq_play(1);
    run_ms(4 * 125 + 90);                        /* step 5 at 625 ms: 35 ms early, past step 4's middle */
    en_key_chord(62, 1);                         /* D4: degree 1 */
    run_ms(3 * 125);
    en_key_chord(62, 0);
    run_ms(50);
    CHECK(en_seq[5].on && en_seq[5].deg == 1 && en_seq[5].len == 3, "the take on step 6: on %d degree %d length %d",
          en_seq[5].on, en_seq[5].deg, en_seq[5].len);
    CHECK(!en_seq[6].on, "the take replaced what it covered");
    CHECK(!en_seq[4].on, "nothing written before it");
    en_seq_record(0);
    en_seq_play(0);

    /* clear, and bring back */
    en_seq_clear();
    run_ms(10);
    for (i = 0; i < EN_SEQ_MAX && !en_seq[i].on; i++)
        ;
    CHECK(i == EN_SEQ_MAX, "cleared");
    en_seq_restore();
    run_ms(10);
    CHECK(en_seq[5].on && en_seq[5].deg == 1, "brought back");

    if (!fails)
        printf("seq: all ok\n");
    return fails != 0;
}
