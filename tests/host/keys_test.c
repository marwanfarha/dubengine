/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the keys' chords. Each key in the scale plays the triad the scale builds on it, voiced as asked, an
 * octave down; held, it sustains; let go, it rings out; with the generative layers off, nothing sounds by itself.
 *   cc -std=c99 -DEN_HOST -Ifirmware/src/dsp tests/host/keys_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (no MIDI out here) */

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t buf[512];
static float peak(int blocks)                    /* the loudest sample over blocks of 256 frames (in 1/32768ths) */
{
    int b, i;
    float pk = 0.0f;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, 2048);
        for (i = 0; i < 512; i++) {
            float v = (float)abs(buf[i]) / 32768.0f;
            if (v > pk)
                pk = v;
        }
    }
    return pk;
}

/* C natural minor (Aeolian): the triad on each degree, root position, as semitones above its root */
static void expect(int note, int voicing, int a, int b, int c, const char *what)
{
    en_voicing_set(voicing);
    en_key_chord(note, 1);
    peak(1);
    CHECK(en_key_note == note, "%s: key %d not held (%d)", what, note, en_key_note);
    CHECK(en_key_tri[0] == a && en_key_tri[1] == b && en_key_tri[2] == c, "%s: got %d %d %d, want %d %d %d", what,
          en_key_tri[0], en_key_tri[1], en_key_tri[2], a, b, c);
    en_key_chord(note, 0);
    peak(1);
}

int main(void)
{
    en_init();
    en_set(P_ROOT, 0);
    en_set(P_MODE, MODE_AEOLIAN);
    en_pool(EN_MODE_POOL[MODE_AEOLIAN]);
    peak(4);
    CHECK(peak(200) < 0.002f, "silent with nothing held");

    /* C4 (60) in C minor: Cm. Root position C3 Eb3 G3; first inversion Eb3 G3 C4; second G3 C4 Eb4 */
    expect(60, EN_VO_ROOT, 48, 51, 55, "Cm root");
    expect(60, EN_VO_FIRST, 51, 55, 60, "Cm first");
    expect(60, EN_VO_SECOND, 55, 60, 63, "Cm second");
    /* D4 (62): D diminished, D F Ab */
    expect(62, EN_VO_ROOT, 50, 53, 56, "Ddim root");
    /* Eb4 (63): Eb major, Eb G Bb */
    expect(63, EN_VO_ROOT, 51, 55, 58, "Eb root");
    /* Bb3 (58): Bb major, Bb D F (the scale wrapped: D and F an octave up) */
    expect(58, EN_VO_ROOT, 46, 50, 53, "Bb root");
    /* a key outside the scale (E4, 64): nothing */
    en_key_chord(64, 1);
    peak(1);
    CHECK(en_key_note == 0, "E4 is outside C minor, yet played");
    en_key_chord(64, 0);

    /* held: it sustains; let go: it rings out */
    en_voicing_set(EN_VO_FIRST);
    peak(400);
    en_key_chord(60, 1);
    {
        const float a = peak(40), b = peak(200);
        CHECK(a > 0.05f, "a held chord is heard (%.3f)", (double)a);
        CHECK(b > 0.5f * a, "a held chord sustains (%.3f then %.3f)", (double)a, (double)b);
    }
    en_key_chord(60, 0);
    {
        const float r = peak(172);               /* the first second: still ringing */
        float t;
        peak(1380);                              /* ~8 s more */
        t = peak(344);                           /* then 2 s: the echo's tail gone */
        CHECK(r > 0.05f, "let go, it rings on a moment (%.3f)", (double)r);
        CHECK(t < 0.001f * r, "let go, it rings out (%.4f after 9 s, from %.3f)", (double)t, (double)r);
    }
    if (!fails)
        printf("keys: all ok\n");
    return fails != 0;
}
