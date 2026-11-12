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
    en_step_t st = {1, 0, 2, EN_VO_ROOT, 2, {0, 0, 0}};
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
    en_meter[0] = en_meter[1] = en_meter[EN_NTRK] = 0.0f;
    en_key_chord(62, 1);
    run_ms(300);
    CHECK(en_meter[1] > 0.02f && en_meter[0] < 0.002f && en_meter[EN_NTRK] > 0.02f, "the meters: B alone (%.4f %.4f %.4f)",
          (double)en_meter[0], (double)en_meter[1], (double)en_meter[EN_NTRK]);
    en_key_chord(62, 0);
    run_ms(4000);
    /* the level: B's down, B quieter; back at 80 as it was */
    sel(1), en_syn_set(SY_AA, 0), en_syn_set(SY_AS, 100), en_syn_set(SY_AR, 0), en_syn_set(SY_DETUNE, 0);
    en_key_chord(62, 1), run_ms(300), a = run_ms(300), en_key_chord(62, 0), run_ms(500);
    en_mix_set(MX_LEVEL, 40);
    en_key_chord(62, 1), run_ms(300), b = run_ms(300), en_key_chord(62, 0), run_ms(500);
    CHECK(b > 0.2f * a && b < 0.3f * a, "Level 40: a quarter (%.2f then %.2f)", (double)a, (double)b);
    en_mix_set(MX_LEVEL, 0);
    en_key_chord(62, 1), run_ms(300), b = run_ms(300), en_key_chord(62, 0), run_ms(500);
    CHECK(b < 0.01f * a, "Level 0: silent (%.3f)", (double)b);
    en_mix_set(MX_LEVEL, 80);
    /* the EQ: flat, 0 dB everywhere; a bell +12 dB at 678 Hz: there and only there; the cuts take the ends away */
    run_ms(20);
    CHECK(en_eq_db(100.0f) == 0.0f && en_eq_db(5000.0f) == 0.0f, "the EQ flat");
    en_mix_set(MX_EQ + EN_EQ_BELL2 * EQ_NF + EQ_GAIN, 12);
    run_ms(20);
    CHECK(en_eq_db(en_eq_hz(51)) > 11.5f && en_eq_db(en_eq_hz(51)) < 12.5f && en_eq_db(60.0f) < 1.0f && en_eq_db(10000.0f) < 1.0f,
          "a bell: +12 dB at its frequency (%.1f, %.1f, %.1f)", (double)en_eq_db(en_eq_hz(51)), (double)en_eq_db(60.0f),
          (double)en_eq_db(10000.0f));
    en_mix_set(MX_EQ + EN_EQ_BELL2 * EQ_NF + EQ_GAIN, 0);
    en_mix_set(MX_EQ + EN_EQ_LOCUT * EQ_NF + EQ_FREQ, 60);   /* (~1.26 kHz) */
    en_mix_set(MX_EQ + EN_EQ_HISHELF * EQ_NF + EQ_GAIN, -15);
    run_ms(20);
    CHECK(en_eq_db(100.0f) < -30.0f && en_eq_db(15000.0f) < -12.0f && en_eq_db(3000.0f) > -4.0f, "a low cut and a high shelf (%.1f %.1f %.1f)",
          (double)en_eq_db(100.0f), (double)en_eq_db(15000.0f), (double)en_eq_db(3000.0f));
    en_key_chord(62, 1), run_ms(300), b = run_ms(300), en_key_chord(62, 0), run_ms(500);
    CHECK(b < 0.7f * a && b > 0.0f, "and heard: the low chord thinner (%.2f then %.2f)", (double)a, (double)b);
    en_mix_set(MX_EQ + EN_EQ_LOCUT * EQ_NF + EQ_FREQ, 0), en_mix_set(MX_EQ + EN_EQ_HISHELF * EQ_NF + EQ_GAIN, 0);
    CHECK(en_trk[0].mx[MX_LEVEL] == 80 && en_trk[0].mx[MX_EQ + EN_EQ_BELL2 * EQ_NF + EQ_GAIN] == 0, "A's mix untouched");
    /* a step's locks: its own Level while it sounds; the next step without one, the page's again */
    en_seq_length(8), en_seq_step(0, &st), en_seq_step(4, &st), en_seq_lock(0, EN_LK_LEVEL, 10);
    run_ms(20);
    CHECK(en_trk[1].seq[0].lock[EN_LK_LEVEL] == 11 && en_trk[1].seq[4].lock[EN_LK_LEVEL] == 0, "the lock kept on its step");
    en_seq_play(1);
    run_ms(60), b = run_ms(150);                 /* step 1, locked low */
    run_ms(350), a = run_ms(150);                /* step 5, as the page has it */
    CHECK(b < 0.3f * a && b > 0.0f, "step 1 locked quiet, step 5 as it was (%.2f then %.2f)", (double)b, (double)a);
    en_seq_play(0);
    st.on = 0, en_seq_step(0, &st), st.on = 1;
    run_ms(20);
    CHECK(en_trk[1].seq[0].lock[EN_LK_LEVEL] == 0, "a step emptied: its locks gone");
    en_seq_clear();
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
    /* the bass and the ping as tracks: a key in the scale plays its note (the meters tell which); outside it, nothing */
    sel(EN_T_BASS), en_set(P_ROOT, 0), en_set(P_MODE, MODE_AEOLIAN);
    sel(EN_T_PING), en_set(P_ROOT, 0), en_set(P_MODE, MODE_AEOLIAN);
    run_ms(20);
    en_meter[EN_T_BASS] = en_meter[EN_T_PING] = 0.0f;
    sel(EN_T_BASS), en_key_chord(61, 1), run_ms(100), en_key_chord(61, 0);
    CHECK(en_meter[EN_T_BASS] == 0.0f, "the bass: a key outside the scale is silent");
    en_key_chord(60, 1), run_ms(200);
    CHECK(en_meter[EN_T_BASS] > 0.02f && en_meter[EN_T_PING] == 0.0f && en_trk[EN_T_BASS].key_note == 60, "the bass plays (%.3f)",
          (double)en_meter[EN_T_BASS]);
    en_key_chord(60, 0), run_ms(500), a = run_ms(100);
    CHECK(a < 0.5f, "let go: gone (%.2f)", (double)a);
    /* Decay under 100: it dies away while held */
    en_vp_set(VP_DEC, 20), en_key_chord(60, 1), run_ms(100), a = run_ms(100), run_ms(1500), b = run_ms(100), en_key_chord(60, 0);
    CHECK(b < 0.1f * a, "the bass's Decay (%.2f then %.2f)", (double)a, (double)b);
    en_vp_set(VP_DEC, 100), run_ms(300);
    /* the ping's delay: with Mix up a repeat comes a dotted eighth later (375 ms at 120); at 0, none */
    sel(EN_T_PING), en_vp_set(VP_REV, 0), en_vp_set(VP_DEC, 0), en_vp_set(VP_DMIX, 0);
    en_key_chord(60, 1), run_ms(50), en_key_chord(60, 0), run_ms(250), a = run_ms(200);
    en_vp_set(VP_DMIX, 80), run_ms(2000);
    en_key_chord(60, 1), run_ms(50), en_key_chord(60, 0), run_ms(250), b = run_ms(200);
    CHECK(a < 0.2f && b > 1.0f, "the ping's delay: none at Mix 0, a repeat at 80 (%.2f, %.2f)", (double)a, (double)b);
    /* a parameter set on the ping leaves the bass's */
    CHECK(en_trk[EN_T_BASS].vp[VP_DMIX] == VP_INFO[VP_DMIX].def[0] && en_trk[EN_T_PING].vp[VP_DMIX] == 80, "each its sound");
    run_ms(6000);
    printf(fails ? "tracks: %d FAILED\n" : "tracks: all ok\n", fails);
    return fails != 0;
}
