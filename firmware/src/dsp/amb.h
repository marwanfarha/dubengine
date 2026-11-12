/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the instrument. Two tracks of dub chords, a bass voice and a ping voice (both waiting for their
 * sequencers), one tempo.
 *
 *   TRACK   (A, B) its own key and scale, its own pattern (up to 64 steps, its own length and step), its own synth
 *           (four voices of two oscillators, a wavefolder, a resonant low-pass, four envelopes, two LFOs), its own
 *           tape delay, its own send to the reverb, its own level.
 *   REVERB  one plate (Dattorro's) for both tracks, each track's send its Reverb page's Mix.
 *   BASS, PING   FM voices (four operators), played by en_bass_note and en_ping_note.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands, which the render drains at the
 * start of a block; the state below marked (UI) is written by the render and only read elsewhere. Float only, no
 * libm: fastmath.h.
 *
 * The selected track, en_sel: the commands posted (keys, steps, parameters) go to it, and the names below without a
 * track (en_seq, en_syn, en_key_note ..) read it. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NPAD 4                      /* a chord's voices */
#define EN_NTRK 2

enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, MODE_HIRAJOSHI, MODE_IN, MODE_IWATO, MODE_YO, MODE_INSEN, MODE_LOCRIAN, EN_NMODES };
extern const char *const EN_NOTE_NAME[12];
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];   /* a mode's notes: bit k, k semitones above the root */

/* the tempo (both tracks'), and the selected track's root (0..11) and mode */
enum { P_TEMPO, P_ROOT, P_MODE, P_NPARAMS };
#define EN_TEMPO_MIN 40
#define EN_TEMPO_MAX 240
void en_init(void);
void en_render(int32_t *out, uint32_t n, uint32_t gain_q12);   /* n stereo frames, 24 bits in 32 */
void en_set(int param, int value);
void en_pool(uint32_t pool);                   /* the selected track's scale, bit k: k semitones above its root */
void en_play(int on);                          /* 0: the pattern stopped (as en_seq_play(0)) */
void en_panic(void);                           /* everything quiet, now */
uint32_t en_queue_w(void);
uint32_t en_queue_drops(void);
uint32_t en_queue_left(void);
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);   /* (the platform's) */
extern volatile uint8_t en_load;               /* (the platform's) the audio's share of its time, % */
extern volatile uint8_t en_sel;                /* the selected track: 0 A, 1 B (the UI's to set) */
extern volatile uint8_t en_tempo;              /* (UI) the tempo as the engine has it */

/* the keys play chords. A key held (its MIDI note, F3 = 53) plays the triad the track's scale builds on it, held while
 * the key is. A key outside the scale: nothing. The voicing: EN_VO_* */
enum { EN_VO_ROOT, EN_VO_FIRST, EN_VO_SECOND, EN_NVO };
#define EN_VO_KEY 3                               /* (a step's: the track's own, the Play page's) */
void en_key_chord(int note, int on);
void en_voicing_set(int v);
enum { EN_Q_MAJ, EN_Q_MIN, EN_Q_DIM, EN_Q_AUG, EN_Q_OTHER };

/* the sequencer. A track's pattern: up to EN_SEQ_MAX steps, each one chord (or none): its degree of the scale (from the
 * root), its octave (-2..+1 around the root's octave, C4..B4: stored +2), its voicing (EN_VO_*, or EN_VO_KEY) and its
 * length in steps. The key and the mode can change under a pattern: it follows them. The step a 1/8, 1/16, 1/32 or a
 * triplet (EN_DIV_*). PLAY starts both tracks together. Recording (playing, REC on): the keys write their chords on
 * the selected track, each on its nearest step, as long as held, replacing what was there. The metronome: a click on
 * each beat, the bar's first louder, while the pattern plays. */
#define EN_SEQ_MAX 64
enum { EN_DIV_8, EN_DIV_16, EN_DIV_32, EN_DIV_8T, EN_DIV_16T, EN_NDIV };
extern const char *const EN_DIV_NAME[EN_NDIV];
typedef struct {
    uint8_t on, deg, oct, voi, len;
    uint8_t lock[3];                             /* its locks (EN_LK_*): 0 none, else the value + 1 (0..100) */
} en_step_t;
enum { EN_LK_LEVEL, EN_LK_CUTOFF, EN_LK_SEND, EN_NLK };   /* a step's own Level, Cutoff and Delay mix, while it sounds */
extern volatile uint8_t en_seq_playing, en_seq_rec, en_metro_on, en_metro_lvl;
void en_seq_play(int on);
void en_seq_record(int on);
void en_seq_length(int steps);                   /* 1..EN_SEQ_MAX */
void en_seq_division(int div);                   /* EN_DIV_* */
void en_seq_clear(void);                         /* the pattern cleared, kept to bring back */
void en_seq_restore(void);                       /* the pattern before the last clear */
void en_seq_step(int i, const en_step_t *st);    /* step i set (st->on 0: emptied, its locks gone); the locks: en_seq_lock */
void en_seq_lock(int i, int which, int v);       /* step i's lock (EN_LK_*): v 0..100, or -1 none */
void en_metro(int on, int level);                /* on 0/1, level 0..100 */
int en_seq_note(const en_step_t *st);            /* a step's root as a MIDI note, in the selected track's key now */
int en_note_step(int note, en_step_t *st);       /* a key's note as a step's degree and octave: 0 if not in the scale */

/* a track's synth, delay and reverb send. Parameters: en_syn[], set by en_syn_set (values as SYN_INFO's ranges). The
 * Reverb pages' parameters are the one plate's: set on either track, both have them; but Mix (SY_RMIX), a track's send */
enum { EN_WAVE_SINE, EN_WAVE_TRI, EN_WAVE_SAW, EN_WAVE_SQUARE, EN_NWAVE };
enum { EN_LS_SINE, EN_LS_TRI, EN_LS_SAW, EN_LS_SQUARE, EN_LS_RANDOM, EN_NLSHAPE };
enum { EN_MD_NONE, EN_MD_PITCH, EN_MD_FOLD, EN_MD_DETUNE, EN_MD_CUTOFF, EN_MD_RESO, EN_MD_LEVEL, EN_MD_AATT, EN_MD_ADEC,
       EN_MD_AREL, EN_MD_ECHO, EN_NMD };
enum {
    SY_WAVE, SY_FOLD, SY_DETUNE, SY_LEVEL,                 /* Osc */
    SY_CUT, SY_RESO, SY_FENV, SY_KTRK,                     /* Filter: cutoff, resonance, the filter env's amount, key track */
    SY_AA, SY_AD, SY_AS, SY_AR,                            /* Amp env */
    SY_FA, SY_FD, SY_FS, SY_FR,                            /* Filter env */
    SY_1A, SY_1D, SY_1S, SY_1R, SY_1DST, SY_1AMT,          /* Env 1 */
    SY_2A, SY_2D, SY_2S, SY_2R, SY_2DST, SY_2AMT,          /* Env 2 */
    SY_L1RATE, SY_L1SHAPE, SY_L1DST, SY_L1AMT, SY_L1SYNC,  /* LFO 1 */
    SY_L2RATE, SY_L2SHAPE, SY_L2DST, SY_L2AMT, SY_L2SYNC,  /* LFO 2 */
    SY_DTIME, SY_DFB, SY_DMIX, SY_DTONE, SY_DWOW, SY_DDRIVE,     /* Delay: time, feedback %, mix, tone, wow, drive */
    SY_RDECAY, SY_RMIX, SY_RPRE, SY_RDAMP, SY_RLOW, SY_RWIDTH,   /* Reverb: decay, mix, pre-delay, damping, low cut, width */
    SY_RSIZE, SY_RHICUT, SY_RDIFF, SY_RMODD, SY_RMODR, SY_RFREEZE,   /* Reverb: size, high cut, diffusion, modulation
                                                                 * depth and rate, freeze */
    SY_DSYNC, SY_DMS,                                      /* Delay: Sync (1) or Free (0); the free time (0..100: 20 ms .. 2 s) */
    SY_DWIDTH, SY_DBLUR,                                   /* Delay 2: the repeats' width, their blur */
    SY_DMODE, SY_DPING,                                    /* Delay 2: a new time Repitch (a tape's glide), Fade or Jump; ping pong */
    SY_N
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_syn_info_t;
extern const en_syn_info_t SYN_INFO[SY_N];
extern const char *const EN_WAVE_NAME[EN_NWAVE];
extern const char *const EN_LS_NAME[EN_NLSHAPE];
extern const char *const EN_MD_NAME[EN_NMD];
#define EN_NSYNC 8                                 /* an LFO's sync: Free, then 4 bars .. 1/16 */
enum { EN_DM_REPITCH, EN_DM_FADE, EN_DM_JUMP, EN_NDMODE };
extern const char *const EN_DMODE_NAME[EN_NDMODE];
#define EN_NDTIME 10                               /* the delay's times: 1/16 .. 1 bar */
extern const char *const EN_DTIME_NAME[EN_NDTIME];
extern const char *const EN_SYNC_NAME[EN_NSYNC];
void en_syn_set(int p, int v);
float en_delay_ms(int v);                          /* the Delay's Free time, SY_DMS's value in ms */

/* a track's place in the mix: its level (0..100: 80 as it is, 100 +4 dB) and its 8-band EQ, on the track with its
 * delay: a low cut and a high cut (12 dB an octave; at their ends, off), a low and a high shelf, four bells. Each band
 * its frequency (0..100: 20 Hz .. 20 kHz), its gain (-15..+15 dB; the cuts: none) and, the bells, its Q (0..100:
 * 0.35 .. 8). Parameters: en_mx[], set by en_mix_set */
#define EN_NEQ 8
enum { EN_EQ_LOCUT, EN_EQ_LOSHELF, EN_EQ_BELL1, EN_EQ_BELL2, EN_EQ_BELL3, EN_EQ_BELL4, EN_EQ_HISHELF, EN_EQ_HICUT };
enum { EQ_FREQ, EQ_GAIN, EQ_Q, EQ_NF };
enum { MX_LEVEL, MX_EQ, MX_N = MX_EQ + EN_NEQ * EQ_NF };   /* (band b's: MX_EQ + b * EQ_NF + EQ_*) */
int en_mix_lo(int p);
int en_mix_hi(int p);
int en_mix_def(int p);
void en_mix_set(int p, int v);
float en_eq_hz(int v);                             /* a band's frequency, its value in Hz */
float en_eq_db(float hz);                          /* (UI) the selected track's EQ at hz, dB */

/* (UI, tests) a track as the engine has it: its key, its pattern and where it plays, the key whose chord sounds (0
 * none) and its notes as voiced, the chord last struck (its root's pitch class, its quality EN_Q_*, a count that moves
 * with each strike), its parameters; and for the screen's pictures the modulation on each target (EN_MD_*), each
 * envelope's level (0..1), stage (0 idle, 1 attack, 2 decay/sustain, 3 release) and seconds in it (the amp's, the
 * filter's, Env 1, Env 2), each LFO's phase (0..1) and value (-1..1), and the values heard: cutoff (Hz), fold, detune */
typedef struct {
    en_step_t seq[EN_SEQ_MAX];
    uint8_t seq_len, seq_div, seq_pos, root, mode, voicing;
    uint16_t pool;
    uint32_t seq_steps;
    uint8_t key_note, key_tri[3], now_pc, now_q, now_n;
    int16_t syn[SY_N], mx[MX_N];
    float syn_amp, md_now[EN_NMD], env_lv[4], env_age[4], lfo_ph[2], lfo_val[2], cut_now, fold_now, det_now;
    uint8_t env_st[4];
} en_trk_t;
extern volatile en_trk_t en_trk[EN_NTRK];
#define en_seq (en_trk[en_sel].seq)
#define en_seq_len (en_trk[en_sel].seq_len)
#define en_seq_div (en_trk[en_sel].seq_div)
#define en_seq_pos (en_trk[en_sel].seq_pos)
#define en_seq_steps (en_trk[en_sel].seq_steps)
#define en_key_note (en_trk[en_sel].key_note)
#define en_key_tri (en_trk[en_sel].key_tri)
#define en_now_pc (en_trk[en_sel].now_pc)
#define en_now_q (en_trk[en_sel].now_q)
#define en_now_n (en_trk[en_sel].now_n)
#define en_syn (en_trk[en_sel].syn)
#define en_mx (en_trk[en_sel].mx)
#define en_syn_amp (en_trk[en_sel].syn_amp)
#define en_md_now (en_trk[en_sel].md_now)
#define en_env_lv (en_trk[en_sel].env_lv)
#define en_env_age (en_trk[en_sel].env_age)
#define en_env_st (en_trk[en_sel].env_st)
#define en_lfo_ph (en_trk[en_sel].lfo_ph)
#define en_lfo_val (en_trk[en_sel].lfo_val)
#define en_cut_now (en_trk[en_sel].cut_now)
#define en_fold_now (en_trk[en_sel].fold_now)
#define en_det_now (en_trk[en_sel].det_now)
extern volatile float en_pad_level;               /* (UI) the louder track's amp envelope, 0..1 */
/* (UI) the peaks since last taken (the reader sets them to 0): track A, track B, the output; 1.0 full scale */
#define EN_NMETER (EN_NTRK + 1)
extern volatile float en_meter[EN_NMETER];

/* the bass and the ping: FM voices, each note played from here until their sequencers come. vel 1..127 */
void en_bass_note(int note, int vel, int ms);
void en_ping_note(int note, int vel);

#if !defined(EN_HOST) && !defined(EN_PROF)
#define EN_PROF 1
#endif
#define EN_PROF_N 4                               /* the render's stages, timed: 0 commands, 1 the tracks, 2 the bass and
                                                  * the pings, 3 the reverb and the output */
#ifdef EN_PROF
uint32_t en_prof_ticks(void);                     /* (the platform's: a free-running counter) */
extern volatile uint32_t en_prof[EN_PROF_N];
#endif
