/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the instrument (amb.h). One compilation unit, -O2. Built on DUB-1's engine (sbegueria/fm1-dub): its FM
 * core, its tape echo and its plate are here, a track's own or shared; its generative layers are gone. */
#include "amb.h"
#include "fastmath.h"

/* on the device the engine's code runs from RAM (app.ld's FAST, copied at boot); what's cold (a step's events, a
 * parameter, the start) is kept in the flash: EN_COLD. (A function in another section isn't inlined into one here, so
 * the hot path stays one section) */
#ifdef EN_HOST
#define EN_COLD
#define EN_POOL
#else
#define EN_COLD __attribute__((section(".text.amb_cold"), noinline))
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")
#define BLK 256
#define NT EN_NTRK
#define NC EN_NCHORD                             /* (the chord tracks: the first two) */
#define VT_BASS EN_T_BASS
#define VT_PING EN_T_PING

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* (Ionian and Aeolian shown as Major and Minor) */
const char *const EN_MODE_NAME[EN_NMODES] = {"Major", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Minor", "Pent maj",
                                             "Pent min", "Hirajosh", "In", "Iwato", "Yo", "Insen", "Locrian"};
#define B(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
const uint16_t EN_MODE_POOL[EN_NMODES] = {
    B(0, 2, 4, 5, 7, 9, 11), B(0, 2, 3, 5, 7, 9, 10), B(0, 1, 3, 5, 7, 8, 10), B(0, 2, 4, 6, 7, 9, 11),
    B(0, 2, 4, 5, 7, 9, 10), B(0, 2, 3, 5, 7, 8, 10), B(0, 2, 4, 7, 9, 9, 9),  B(0, 3, 5, 7, 10, 10, 10),
    B(0, 2, 3, 7, 8, 8, 8), B(0, 1, 5, 7, 8, 8, 8), B(0, 1, 5, 6, 10, 10, 10), B(0, 2, 5, 7, 9, 9, 9),
    B(0, 1, 5, 7, 10, 10, 10), B(0, 1, 3, 5, 6, 8, 10)};   /* (Hirajoshi 1 2 b3 5 b6, In 1 b2 4 5 b6, Iwato 1 b2 4 b5
                                   * b7, Yo 1 2 4 5 6, Insen 1 b2 4 5 b7, Locrian 1 b2 b3 4 b5 b6 b7) */
#undef B

#ifdef EN_PROF
volatile uint32_t en_prof[EN_PROF_N];
#define PROF_B(v) const uint32_t v = en_prof_ticks()
#define PROF_E(i, v) (en_prof[i] += en_prof_ticks() - (v))
#else
#define PROF_B(v) (void)0
#define PROF_E(i, v) (void)0
#endif

/* ------------------------------------------------------------ commands --- */
/* a command: its kind and the track it is for (the one selected as it was posted), a byte, 16 bits */
enum { C_SET, C_POOL, C_PANIC, C_KEY, C_VOICING, C_SEQ, C_STEP, C_SYN, C_BASS, C_PING, C_MIX, C_LOCK, C_VP };
#define QN 1024u                                 /* (a boot's load: four tracks' steps and sounds) */
static uint32_t q[QN];
static volatile uint32_t q_w, q_r, q_drop;
volatile uint8_t en_sel;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN) {
        q_drop++;
        return;
    }
    q[w % QN] = (c | (uint32_t)(en_sel < NT ? en_sel : 0) << 5) << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_pool(uint32_t pool) { post(C_POOL, 0, (int)(pool & 0xFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
EN_COLD void en_key_chord(int note, int on) { post(C_KEY, on != 0, note & 0x7F); }
EN_COLD void en_voicing_set(int v) { post(C_VOICING, v, 0); }
enum { SQ_PLAY, SQ_REC, SQ_LEN, SQ_DIV, SQ_CLEAR, SQ_RESTORE, SQ_METRO };
EN_COLD void en_seq_play(int on) { post(C_SEQ, SQ_PLAY, on != 0); }
EN_COLD void en_play(int on) { if (!on) post(C_SEQ, SQ_PLAY, 0); }
EN_COLD void en_seq_record(int on) { post(C_SEQ, SQ_REC, on != 0); }
EN_COLD void en_seq_length(int steps) { post(C_SEQ, SQ_LEN, steps); }
EN_COLD void en_seq_division(int div) { post(C_SEQ, SQ_DIV, div); }
EN_COLD void en_seq_clear(void) { post(C_SEQ, SQ_CLEAR, 0); }
EN_COLD void en_seq_restore(void) { post(C_SEQ, SQ_RESTORE, 0); }
EN_COLD void en_metro(int on, int level) { post(C_SEQ, SQ_METRO, (on != 0) << 8 | (level & 0x7F)); }
EN_COLD void en_seq_step(int i, const en_step_t *st)      /* packed: on, degree (3 bits), octave (2), voicing (2), length - 1 (6) */
{
    const int len = st->len < 1 ? 1 : st->len > EN_SEQ_MAX ? EN_SEQ_MAX : st->len;
    post(C_STEP, i & 0x3F, (st->on != 0) | (st->deg & 7) << 1 | (st->oct & 3) << 4 | (st->voi & 3) << 6 | (len - 1) << 8);
}
EN_COLD void en_syn_set(int p, int v) { post(C_SYN, p, v); }
EN_COLD void en_mix_set(int p, int v) { post(C_MIX, p, v); }
EN_COLD void en_vp_set(int p, int v) { post(C_VP, p, v); }
EN_COLD void en_seq_lock(int i, int which, int v)
{
    post(C_LOCK, i & 0x3F, (which & 3) << 8 | (v < 0 ? 0 : v > 100 ? 101 : v + 1));
}
EN_COLD void en_bass_note(int note, int vel, int ms)   /* (the length in 20 ms: up to 10 s) */
{
    const int u = ms < 20 ? 1 : ms > 10000 ? 500 : ms / 20;
    post(C_BASS, vel & 0x7F, (note & 0x7F) | u << 7);
}
EN_COLD void en_ping_note(int note, int vel) { post(C_PING, vel & 0x7F, note & 0x7F); }
uint32_t en_queue_w(void) { return q_w; }
uint32_t en_queue_drops(void) { return q_drop; }
uint32_t en_queue_left(void) { return q_w - q_r; }

/* -------------------------------------------------------------- helpers --- */
volatile uint8_t en_load;
static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* a soft clip, cheap: x - 4/27 x^3, flat at +-1.5 (to +-1) */
static inline float soft(float x)                /* tanh, nearly: x (27 + x^2) / (27 + 9 x^2), 1 past 3 */
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float sat(float x)
{
    if (x > 1.5f)
        return 1.0f;
    if (x < -1.5f)
        return -1.0f;
    return x - 0.148148148f * x * x * x;
}

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

static inline float blep_saw(float t, float dt)
{
    float y = 2.0f * t - 1.0f;
    if (t < dt) {
        float x = t / dt;
        y -= x + x - x * x - 1.0f;
    } else if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        y -= x * x + x + x + 1.0f;
    }
    return y;
}

static void svf_coefs(float *c, float f, float k)   /* TPT SVF (Zavalishin): a1, a2, a3, k */
{
    float g = fm_tanf(FM_PI * fm_clampf(f, 40.0f, 16000.0f) * (1.0f / EN_SR));
    c[0] = 1.0f / (1.0f + g * (g + k));
    c[1] = g * c[0];
    c[2] = g * c[1];
    c[3] = k;
}

/* ---------------------------------------------------------- the FM core --- */
/* Four sine operators, as the 4-op FM synths have them (the DX21 / TX81Z family's eight algorithms; op 4 can
 * feed back on itself), and a ninth "algorithm", two operators only (2 into 1), for the light voices. Each
 * operator: a ratio to the note, a level (a modulator's: its index, in turns; a carrier's: its share of the
 * sound) and a decay to a sustain (a share of its level). The levels are worked out at the control rate
 * (every 32 samples), the operators every sample. The algorithms, 4 -> 3 meaning 4 modulates 3:
 *   1: 4 -> 3 -> 2 -> 1            2: (3 + 4) -> 2 -> 1         3: (3 -> 2) + 4 -> 1
 *   4: (4 -> 3) + 2 -> 1           5: 4 -> 3, 2 -> 1 (two pairs) 6: 4 -> 1, 2 and 3
 *   7: 4 -> 3, and 2 and 1         8: all four, side by side     9: 2 -> 1 (two operators) */
#define FM_NALG 9
#define FM_ALG2 8                                /* the two-operator one */
static const uint8_t FM_CARR[FM_NALG] = {1, 1, 1, 1, 5, 7, 7, 15, 1};   /* the carriers (bit k: op k + 1) */
typedef struct {
    float ratio, level, dec, sus;               /* the ratio, the level, the decay (s) to the sustain (0..1 of the level) */
} fmop_t;
typedef struct {
    uint8_t alg;                                /* 0..8 */
    float fb;                                   /* op 4's feedback (turns) */
    fmop_t op[4];
} fmpatch_t;
typedef struct {
    float ph[4], inc[4], e[4], kd[4], lv[4], fb1, fb2;
} fmvoice_t;

/* a note: the operators' rates (rmul: op 2's ratio moved), their envelopes from the top (dmul: the modulators'
 * decays stretched) */
static void fm_note(fmvoice_t *v, const fmpatch_t *p, float f, float rmul, float qmul, float dmul)
{
    int k;
    for (k = 0; k < 4; k++) {
        float r = p->op[k].ratio * (k == 1 ? rmul : k >= 2 ? qmul : 1.0f), d = p->op[k].dec * (k ? dmul : 1.0f);
        v->inc[k] = f * r * (1.0f / EN_SR);
        v->e[k] = 1.0f;
        v->kd[k] = d > 0.0f ? fm_expf(-32.0f / (d * EN_SR)) : 1.0f;
    }
}

static void fm_tune(fmvoice_t *v, const fmpatch_t *p, float f, float rmul, float qmul)   /* a slide: the rates only */
{
    int k;
    for (k = 0; k < 4; k++)
        v->inc[k] = f * p->op[k].ratio * (k == 1 ? rmul : k >= 2 ? qmul : 1.0f) * (1.0f / EN_SR);
}

/* the control rate: the envelopes a step on, the levels (ms: the modulators' scaled, as Colour and Tone have it) */
static void fm_ctl(fmvoice_t *v, const fmpatch_t *p, int alg, float ms)
{
    int k;
    for (k = 0; k < 4; k++) {
        float s = p->op[k].sus;
        v->e[k] = s + (v->e[k] - s) * v->kd[k];
        v->lv[k] = p->op[k].level * v->e[k] * ((FM_CARR[alg] >> k & 1u) ? 1.0f : ms);
    }
}

/* one sample (dk: the pitch now, a multiple of the note's; fb: op 4's feedback) */
static inline float fm_sample(fmvoice_t *v, int alg, float fb, float dk)
{
    float o2, o3, o4, out, *ph = v->ph, *l = v->lv;
    int k;
    if (alg == FM_ALG2) {
        o2 = sine(ph[1] + 8.0f + fb * (v->fb1 + v->fb2)) * l[1];
        v->fb2 = v->fb1;
        v->fb1 = o2;
        out = sine(ph[0] + o2 + 8.0f) * l[0];
        ph[0] += v->inc[0] * dk;
        ph[1] += v->inc[1] * dk;
        for (k = 0; k < 2; k++)
            if (ph[k] >= 1.0f)
                ph[k] -= 1.0f;
        return out;
    }
    o4 = sine(ph[3] + 8.0f + fb * (v->fb1 + v->fb2)) * l[3];   /* (the feedback: the last two, averaged) */
    v->fb2 = v->fb1;
    v->fb1 = o4;
    switch (alg) {
    case 0: o3 = sine(ph[2] + o4 + 8.0f) * l[2]; o2 = sine(ph[1] + o3 + 8.0f) * l[1]; out = sine(ph[0] + o2 + 8.0f) * l[0]; break;
    case 1: o3 = sine(ph[2] + 8.0f) * l[2]; o2 = sine(ph[1] + o3 + o4 + 8.0f) * l[1]; out = sine(ph[0] + o2 + 8.0f) * l[0]; break;
    case 2: o3 = sine(ph[2] + 8.0f) * l[2]; o2 = sine(ph[1] + o3 + 8.0f) * l[1]; out = sine(ph[0] + o2 + o4 + 8.0f) * l[0]; break;
    case 3: o3 = sine(ph[2] + o4 + 8.0f) * l[2]; o2 = sine(ph[1] + 8.0f) * l[1]; out = sine(ph[0] + o2 + o3 + 8.0f) * l[0]; break;
    case 4: o3 = sine(ph[2] + o4 + 8.0f) * l[2]; o2 = sine(ph[1] + 8.0f) * l[1]; out = sine(ph[0] + o2 + 8.0f) * l[0] + o3; break;
    case 5: o3 = sine(ph[2] + o4 + 8.0f) * l[2]; o2 = sine(ph[1] + o4 + 8.0f) * l[1]; out = sine(ph[0] + o4 + 8.0f) * l[0] + o2 + o3; break;
    case 6: o3 = sine(ph[2] + o4 + 8.0f) * l[2]; o2 = sine(ph[1] + 8.0f) * l[1]; out = sine(ph[0] + 8.0f) * l[0] + o2 + o3; break;
    default: o3 = sine(ph[2] + 8.0f) * l[2]; o2 = sine(ph[1] + 8.0f) * l[1]; out = sine(ph[0] + 8.0f) * l[0] + o2 + o3 + o4; break;
    }
    for (k = 0; k < 4; k++) {
        ph[k] += v->inc[k] * dk;
        if (ph[k] >= 1.0f)
            ph[k] -= 1.0f;
    }
    return out;
}

/* the FM voices' patches: the chord's Keys (an electric piano: a body pair, and a tine pair at 14) and Bell
 * (3.5 against 1, 7 against 2); the basses: Punch (a stack, a thump in the first 90 ms), Rubber (two pairs, an
 * octave under, op 4 feeding back), Pluck (2 ops at 3, gone in 50 ms), Growl (2 ops at 1, a bite) */
static const fmpatch_t FM_KEYS = {4, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.3f, 0.35f, 0.15f}, {1.0f, 0.4f, 0.0f, 1.0f},
                                            {14.0f, 0.13f, 0.05f, 0.0f}}};
static const fmpatch_t FM_BELL = {4, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {3.5f, 0.38f, 0.5f, 0.12f}, {2.0f, 0.45f, 0.0f, 1.0f},
                                            {7.07f, 0.16f, 0.18f, 0.0f}}};
static const fmpatch_t FM_PUNCH = {0, 0.12f, {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.5f, 0.06f, 0.08f}, {2.0f, 0.22f, 0.12f, 0.1f},
                                             {1.0f, 0.12f, 0.2f, 0.25f}}};
static const fmpatch_t FM_RUBBER = {4, 0.3f, {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.3f, 0.25f, 0.3f}, {0.5f, 0.6f, 0.0f, 1.0f},
                                              {1.0f, 0.18f, 0.3f, 0.4f}}};
static const fmpatch_t FM_PLUCK = {FM_ALG2, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {3.0f, 0.55f, 0.035f, 0.0f}, {2.0f, 0.2f, 0.05f, 0.0f},
                                                   {1.0f, 0.1f, 0.1f, 0.0f}}};
static const fmpatch_t FM_GROWL = {FM_ALG2, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.44f, 0.07f, 0.09f}, {2.0f, 0.2f, 0.1f, 0.1f},
                                                   {1.0f, 0.1f, 0.1f, 0.0f}}};
/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni): a predelay, four input diffusers,
 * then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an
 * allpass and a delay, feeding the other half. Stereo out from seven taps a side. The paper's lengths
 * are for 29761 Hz; DS() scales them to 44.1 kHz. */
/* (the classics pass: the plate at half the rate, 22.05 kHz: a quarter of the engine's time halved, its memory too;
 * nothing of it above ~10 kHz was heard: its input band-limited, its damping) */
#define DS(n) (((n) * 7409 + 5000) / 10000)           /* 22050 / 29761 */
#define PRE_N 2205                                   /* (100 ms of pre-delay) */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {1, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (1 + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
/* (the pre-delay its own line, int16 at 1/16384 (+-2), so 100 ms fits the pool's headroom) */
static int16_t pre_buf[PRE_N];
static int pre_i;
static inline void pre_push(float x)
{
    float v = x * 16384.0f;
    pre_buf[pre_i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    if (++pre_i >= PRE_N)
        pre_i = 0;
}
static inline float pre_tap(int k)
{
    int j = pre_i - k;
    return (float)pre_buf[j < 0 ? j + PRE_N : j] * (1.0f / 16384.0f);
}
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_decay_t = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;
static float pl_acc, pl_ol, pl_or;               /* (the plate at half the rate: the last input, its last output) */
static uint32_t pl_ph;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = fm_flush(x);                    /* (a tail fading to nothing: no denormals left going round) */
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

/* the Reverb pages: the lines' size, the diffusion, the modulation's depth and rate, the pre-delay, the damping, freeze */
static float de_rv_sz = 1.0f, de_rv_diff = 1.0f, de_rv_md = 1.0f, de_rv_mr = 0.0002f, de_pdamp = 0.64f;
static int de_pre = 662;
static uint8_t de_rfreeze;
static inline void reverb(float in, float *ol, float *or_)
{
    /* Size scales where the lines are read, Diffusion the allpasses' gains, Mod the lines' wobble */
    const float sz = de_rv_sz, df = de_rv_diff, md = de_rv_md, mr = de_rv_mr;
#define SZ(n) ((int)((float)DS(n) * sz))
    float x, a, b, fb_l = tap(&dl[L_D2R], SZ(3163)), fb_r = tap(&dl[L_D2L], SZ(3720));
    pre_push(in);
    x = pre_tap(de_pre);
    pl_bw += 0.84f * (x - pl_bw);                /* (0.6 at the full rate) */
    x = allpass(&dl[L_IN1], DS(142), pl_bw, fm_minf(0.75f * df, 0.9f));
    x = allpass(&dl[L_IN2], DS(107), x, fm_minf(0.75f * df, 0.9f));
    x = allpass(&dl[L_IN3], DS(379), x, fm_minf(0.625f * df, 0.9f));
    x = allpass(&dl[L_IN4], DS(277), x, fm_minf(0.625f * df, 0.9f));
    {
        float s = lfo_s + mr * lfo_c, c = lfo_c - mr * s;   /* (the same speed at half the rate) */
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)SZ(672) + (float)EXC * md * lfo_s, x + pl_decay * fb_l, -fm_minf(0.7f * df, 0.9f));
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], SZ(4453));
    pl_damp_l += de_pdamp * (b - pl_damp_l);   /* (0.4 at the full rate: DUB-1's 0.64) */
    b = allpass(&dl[L_AP2L], SZ(1800), pl_damp_l * pl_decay, fm_minf(pl_dd2 * df, 0.9f));
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)SZ(908) + (float)EXC * md * lfo_c, x + pl_decay * fb_r, -fm_minf(0.7f * df, 0.9f));
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], SZ(4217));
    pl_damp_r += de_pdamp * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], SZ(2656), pl_damp_r * pl_decay, fm_minf(pl_dd2 * df, 0.9f));
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], SZ(266)) + tap(&dl[L_D1R], SZ(2974)) - tap(&dl[L_AP2R], SZ(1913)) +
                  tap(&dl[L_D2R], SZ(1996)) - tap(&dl[L_D1L], SZ(1990)) - tap(&dl[L_AP2L], SZ(187)) -
                  tap(&dl[L_D2L], SZ(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], SZ(353)) + tap(&dl[L_D1L], SZ(3627)) - tap(&dl[L_AP2L], SZ(1228)) +
                   tap(&dl[L_D2L], SZ(2673)) - tap(&dl[L_D1R], SZ(2111)) - tap(&dl[L_AP2R], SZ(335)) -
                   tap(&dl[L_D2R], SZ(121)));
#undef SZ
}

EN_COLD static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    for (i = 0; i < PRE_N; i++)
        pre_buf[i] = 0;
    pre_i = 0;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* --------------------------------------------------------------- tracks --- */
volatile en_trk_t en_trk[NT];
volatile uint8_t en_tempo = 120, en_seq_playing, en_seq_rec, en_metro_on, en_metro_lvl = 60;
volatile float en_pad_level, en_meter[EN_NMETER];

#define TAPE_N 33000                             /* a track's tape: 3 s at 11.025 kHz (a bar at 80 bpm; the pool holds two) */
#define TAPE_SR 11025.0f
#define TAPE_FS 40000.0f                          /* the tape's full scale: +-0.82 (the loop's soft clip holds it to 0.7) */
#define EAP_FB 994                               /* the feedback's diffusers: 37 + 89 + 151 + 276 + 441 (3 to 40 ms) */
enum { ENV_AMP, ENV_FILT, ENV_1, ENV_2, NENV };
enum { ST_IDLE, ST_ATT, ST_DEC, ST_REL };
/* a track, the render's own: the key held and its chord's notes, the pattern before the last clear, its step's clock
 * (samples to the next step, samples its chord is still held), the take being recorded; the synth (envelopes,
 * oscillators' phases, the filter, the LFOs); the sends; the tape */
typedef struct {
    uint8_t key_held, notes[EN_NPAD], ready;
    en_step_t kept[EN_SEQ_MAX];
    float sq_left, sq_gate;
    int rec_step;
    uint32_t rec_t0;
    struct {
        float lv;
        uint8_t st;
    } env[NENV];
    float ph[EN_NPAD][2], s[4], c[4], lfo_ph[2], rnd[2], amp_s;
    float dmix, rmix, dry, wow, drive;
    uint8_t lk[EN_NLK];                          /* the sounding step's locks (0 none, else the value + 1) */
    float gain, gain_s;                          /* the level, and as heard (eased) */
    struct {
        float b0, b1, b2, a1, a2;
        uint8_t on;
    } eq[EN_NEQ];
    float eqz[EN_NEQ][4];                        /* (the bands' states: left, right) */
    int tape_w, tape_ph, eap_i[5];
    int32_t quiet;                               /* tape samples written silent in a row (past the tape's length: empty) */
    uint8_t dmode, ping;                         /* a new time: EN_DM_*; ping pong */
    float len_b, xf;                             /* (Fade: the time faded to, how far, 0..1) */
    float fbn;                                   /* (1 / the feedback's band-pass at its loudest) */
    float tape_acc, tape_lp, tape_hp, p1, c1, p3, c3, len, len_t, fb, hpc, lpc, in_lp, in_hp, in_c, blur, wide, wph1,
          wph2, dk, dd;
} tk_t;
static tk_t tk[NT];
static int16_t tape[NC][TAPE_N] EN_POOL;
static int16_t eap[NC][EAP_FB];                  /* (int16 at 1/20000: +-1.6; in the RAM: the pool is the tapes' and the plate's) */
static float pan_l[EN_NPAD], pan_r[EN_NPAD];     /* a chord's voices across the field */
static float mixl[BLK], mixr[BLK], rsend[BLK], tl[BLK], tr[BLK], es[BLK], rs[BLK];
static uint32_t sq_t;                            /* samples since the start */
static float sq_beat_left;                       /* samples to the next beat */
static uint32_t sq_beat;
static float ck_ph, ck_inc, ck_amp;              /* the metronome's click: a short sine */

static float quarter(void) { return 60.0f * EN_SR / (float)en_tempo; }
static float step_samples(int t)
{
    static const float PER_Q[EN_NDIV] = {2.0f, 4.0f, 8.0f, 3.0f, 6.0f};   /* steps a quarter */
    const int d = en_trk[t].seq_div;
    return quarter() / PER_Q[d < EN_NDIV ? d : EN_DIV_16];
}

/* ------------------------------------------------------------ the echo --- */
/* A track's dub echo (DUB-1's): a tape loop at a quarter rate (11 kHz, 16-bit, mono). In the feedback, a band-pass
 * (each repeat thinner and darker: Tone) and five allpasses (each repeat more dissolved: Blur), then a soft clip: the
 * feedback may pass 100% and the echo swell on itself without running away. What goes in is band-limited (Tone) and
 * may be driven (Drive); the tape's speed sways (Wow). The right side reads a few ms later (Width). A new time is
 * glided to, as a tape's. */
const char *const EN_DMODE_NAME[EN_NDMODE] = {"Repitch", "Fade", "Jump"};
const char *const EN_DTIME_NAME[EN_NDTIME] = {"1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/2.", "1 bar"};
static const float DTIME_BEATS[EN_NDTIME] = {0.25f, 0.3333333f, 0.5f, 0.75f, 0.6666667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
EN_COLD float en_delay_ms(int v) { return 20.0f * fm_exp2f((float)v * 0.0664f); }   /* Free: 20 ms .. 2 s */
EN_COLD static void echo_time(int t)             /* the Delay page's time: Sync's division, or Free's ms */
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    float s;
    if (E->syn[SY_DSYNC])
        s = DTIME_BEATS[(unsigned)E->syn[SY_DTIME] < EN_NDTIME ? E->syn[SY_DTIME] : 3] * 60.0f / (float)en_tempo;
    else
        s = en_delay_ms(E->syn[SY_DMS]) * 0.001f;
    if (s * TAPE_SR > (float)(TAPE_N - 8) * (E->syn[SY_DPING] ? 0.5f : 1.0f))   /* (ping pong: the loop twice the time) */
        s = (float)(TAPE_N - 8) * (E->syn[SY_DPING] ? 0.5f : 1.0f) / TAPE_SR;
    T->len_t = s * TAPE_SR;
    if (T->len <= 0.0f)
        T->len = T->len_t;
}
static inline float tape_read(const int16_t *tp, int w, float d)   /* d quarter-rate samples ago */
{
    int k = (int)d, i0, i1;
    float f = d - (float)k, a, c;
    i0 = w - k;
    i1 = i0 - 1;
    if (i0 < 0)
        i0 += TAPE_N;
    if (i1 < 0)
        i1 += TAPE_N;
    a = (float)tp[i0];
    c = (float)tp[i1];
    return (a + (c - a) * f) * (1.0f / TAPE_FS);
}
static inline int16_t s16(float x)               /* (rounded: a quiet tail fades evenly, not cut short) */
{
    x *= TAPE_FS;
    x = fm_fabsf(x) < 0.75f ? 0.0f : x + (x < 0.0f ? -0.5f : 0.5f);   /* (under a step: nothing, so a tail ends) */
    return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
}
static inline float eapf(tk_t *T, int16_t *ap, int k, int off, int n, float x)
{
    int16_t *b = ap + off;
    float d = (float)b[T->eap_i[k]] * (1.0f / 20000.0f), y = d - T->blur * x, v = (x + T->blur * y) * 20000.0f;
    v = fm_fabsf(v) < 0.75f ? 0.0f : v + (v < 0.0f ? -0.5f : 0.5f);
    b[T->eap_i[k]] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    if (++T->eap_i[k] >= n)
        T->eap_i[k] = 0;
    return y;
}
/* the heads at a time d: what comes out left and right, and what goes round (the diffusers' delay earlier). Ping pong:
 * the loop twice as long; the left hears it at d, the right at 2 d, a feedback on (the repeats left, right, left) */
static inline void heads(const tk_t *T, const int16_t *tp, float d, float *l, float *r, float *f)
{
    if (T->ping) {
        const float d2 = fm_minf(2.0f * d, (float)(TAPE_N - 4));
        *l = tape_read(tp, T->tape_w, d);
        *r = tape_read(tp, T->tape_w, d2) * fm_minf(T->fb, 1.0f);
        *f = tape_read(tp, T->tape_w, fm_maxf(d2 - (float)EAP_FB, 2.0f));
    } else {
        *l = tape_read(tp, T->tape_w, d);        /* (what comes out, on its time) */
        *r = tape_read(tp, T->tape_w, fm_minf(d + 88.0f * fm_minf(T->wide, 2.0f), (float)(TAPE_N - 4)));   /* (the right's:
                                                  * 8 ms later at Width 50, 16 at 100) */
        *f = tape_read(tp, T->tape_w, fm_maxf(d - (float)EAP_FB, 2.0f));
    }
}
/* one sample: the input (the track's send) to the tape, its repeats out (el, er) */
static inline void echo_step(tk_t *T, int16_t *tp, int16_t *ap, float in, float *el, float *er)
{
    T->tape_acc += in;
    if (++T->tape_ph >= 4) {                     /* the tape, a quarter of the time */
        float d, h1, h3, f1, fb, x, w, g;
        T->tape_ph = 0;
        /* a new time. Repitch: glided to, as a tape's speed (the repeats bend); Jump: at once; Fade: the old time's
         * repeats faded into the new time's over ~55 ms, no bend */
        if (T->dmode == EN_DM_JUMP) {
            T->len = T->len_t, T->xf = 0.0f;
        } else if (T->dmode == EN_DM_FADE) {
            if (T->xf <= 0.0f && T->len != T->len_t)
                T->len_b = T->len_t, T->xf = 1e-6f;
        } else {
            T->len += (T->len_t - T->len) * 0.001f;
            T->xf = 0.0f;
        }
        if (T->len > (float)(TAPE_N - 8))
            T->len = (float)(TAPE_N - 8);
        d = T->len;
        if (T->wow > 0.0f) {                     /* Wow: a slow sway (~0.6 Hz) and a faster flutter (~6.5 Hz) */
            T->wph1 += 0.6f / TAPE_SR;
            T->wph2 += 6.5f / TAPE_SR;
            if (T->wph1 >= 1.0f)
                T->wph1 -= 1.0f;
            if (T->wph2 >= 1.0f)
                T->wph2 -= 1.0f;
            d *= 1.0f + T->wow * 0.0035f * (0.8f * sine(T->wph1) + 0.2f * sine(T->wph2));
        }
        if (d > (float)(TAPE_N - 4))
            d = (float)(TAPE_N - 4);
        heads(T, tp, d, &h1, &h3, &f1);
        if (T->xf > 0.0f) {                      /* (Fade: on the way to the new time) */
            float a1, a3, af;
            heads(T, tp, fm_minf(T->len_b, (float)(TAPE_N - 4)), &a1, &a3, &af);
            h1 += (a1 - h1) * T->xf, h3 += (a3 - h3) * T->xf, f1 += (af - f1) * T->xf;
            if ((T->xf += 1.0f / 600.0f) >= 1.0f)
                T->len = T->len_b, T->xf = 0.0f;
        }
        T->tape_lp += T->lpc * (f1 - T->tape_lp);   /* each time round: thinner, darker */
        T->tape_hp += T->hpc * (T->tape_lp - T->tape_hp);
        fb = T->tape_lp - T->tape_hp;
        fb = eapf(T, ap, 4, 553, 441, eapf(T, ap, 3, 277, 276, eapf(T, ap, 2, 126, 151, eapf(T, ap, 1, 37, 89, eapf(T, ap, 0, 0, 37, fb)))));
        x = T->tape_acc * 0.25f;
        if (T->drive > 0.0f) {                   /* Drive: the tape pushed, saturating, its level kept */
            const float k = 1.0f + 5.0f * T->drive;
            x = sat(x * k) * (1.0f + 0.5f * T->drive) / k;
        }
        T->in_lp += T->in_c * (x - T->in_lp);
        T->in_hp += 0.18f * (T->in_lp - T->in_hp);   /* (~350 Hz: the repeats above the chord's low mids) */
        /* the loop's gain: Feedback over what the band-pass takes at its loudest (fbn), so 100% holds for ever and
         * more swells, into the soft clip; ping pong's loop is two repeats long */
        g = fm_minf(T->fb, 2.0f) * (T->ping ? fm_minf(T->fb, 2.0f) : 1.0f) * T->fbn;
        w = T->in_lp - T->in_hp + 0.7f * sat(fb * g * (1.0f / 0.7f));
        if (++T->tape_w >= TAPE_N)
            T->tape_w = 0;
        tp[T->tape_w] = s16(w);
        T->quiet = tp[T->tape_w] ? 0 : T->quiet + 1;
        T->tape_acc = 0.0f;
        T->p1 = T->c1;
        T->p3 = T->c3;
        T->c1 = h1;
        T->c3 = h3;
    }
    {   /* between the tape's samples: a line from the last to the next */
        const float f = (float)T->tape_ph * 0.25f, h1 = T->p1 + (T->c1 - T->p1) * f, h3 = T->p3 + (T->c3 - T->p3) * f;
        if (T->ping) {
            *el = h1;
            *er = h3;
        } else {
            *el = 0.70710678f * h1;
            *er = 0.70710678f * (h1 + fm_minf(0.6f * T->wide, 1.0f) * (h3 - h1));
        }
    }
}

/* ------------------------------------------------------------ the synth --- */
/* A track's chord: four voices (the chord's notes), each two oscillators detuned apart (sine, triangle, saw or square)
 * -> a wavefolder (a sine fold: Fold drives it harder) -> one resonant low-pass for the chord, left and right (the
 * voices spread across) -> the amp, its envelope -> dry, and sends to the track's echo and the plate.
 * Four ADSR envelopes: the amp's, the filter's (on the cutoff by Env amount), Env 1 and Env 2 (each to a target); two
 * LFOs (each to a target). The envelopes, the LFOs and the modulation are worked out every 32 samples. */
const char *const EN_WAVE_NAME[EN_NWAVE] = {"Sine", "Tri", "Saw", "Square"};
const char *const EN_LS_NAME[EN_NLSHAPE] = {"Sine", "Tri", "Saw", "Square", "Random"};
const char *const EN_MD_NAME[EN_NMD] = {"Off", "Pitch", "Fold", "Detune", "Cutoff", "Reso", "Level", "Amp A", "Amp D",
                                        "Amp R", "Delay mix"};
const char *const EN_SYNC_NAME[EN_NSYNC] = {"Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"};
static const float SYNC_BEATS[EN_NSYNC] = {0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f};
const en_syn_info_t SYN_INFO[SY_N] = {
    {"Wave", 0, EN_NWAVE - 1, EN_WAVE_SAW}, {"Fold", 0, 100, 0}, {"Detune", 0, 100, 20}, {"Level", 0, 100, 70},
    {"Cutoff", 0, 100, 48}, {"Reso", 0, 100, 30}, {"Env amt", -100, 100, 40}, {"Key trk", 0, 100, 50},
    {"Attack", 0, 100, 4}, {"Decay", 0, 100, 45}, {"Sustain", 0, 100, 55}, {"Release", 0, 100, 40},
    {"Attack", 0, 100, 0}, {"Decay", 0, 100, 35}, {"Sustain", 0, 100, 0}, {"Release", 0, 100, 35},
    {"Attack", 0, 100, 20}, {"Decay", 0, 100, 50}, {"Sustain", 0, 100, 0}, {"Release", 0, 100, 40},
    {"Target", 0, EN_NMD - 1, EN_MD_NONE}, {"Depth", -100, 100, 0},
    {"Attack", 0, 100, 20}, {"Decay", 0, 100, 50}, {"Sustain", 0, 100, 0}, {"Release", 0, 100, 40},
    {"Target", 0, EN_NMD - 1, EN_MD_NONE}, {"Depth", -100, 100, 0},
    {"Rate", 0, 100, 30}, {"Shape", 0, EN_NLSHAPE - 1, EN_LS_SINE}, {"Target", 0, EN_NMD - 1, EN_MD_NONE},
    {"Depth", -100, 100, 0}, {"Sync", 0, EN_NSYNC - 1, 0},
    {"Rate", 0, 100, 20}, {"Shape", 0, EN_NLSHAPE - 1, EN_LS_TRI}, {"Target", 0, EN_NMD - 1, EN_MD_NONE},
    {"Depth", -100, 100, 0}, {"Sync", 0, EN_NSYNC - 1, 0},
    {"Time", 0, EN_NDTIME - 1, 3}, {"Feedback", 0, 110, 55}, {"Mix", 0, 100, 35}, {"Tone", 0, 100, 45},
    {"Wow", 0, 100, 25}, {"Drive", 0, 100, 20},
    {"Decay", 0, 100, 55}, {"Mix", 0, 100, 25}, {"Pre-dly", 0, 100, 20}, {"Damping", 0, 100, 50},
    {"Low cut", 0, 100, 30}, {"Width", 0, 100, 70},
    {"Size", 0, 100, 80}, {"High cut", 0, 100, 70}, {"Diffusion", 0, 100, 75}, {"Mod depth", 0, 100, 40},
    {"Mod rate", 0, 100, 30}, {"Freeze", 0, 1, 0},
    {"Mode", 0, 1, 1}, {"Time", 0, 100, 60},
    {"Width", 0, 100, 50}, {"Blur", 0, 100, 50},
    {"Mode", 0, EN_NDMODE - 1, EN_DM_REPITCH}, {"Ping pong", 0, 1, 0},
};
/* the plate's own, from the Reverb pages (either track's: both have them) */
static float de_rlow, de_rwidth = 1.0f, de_rhp, de_rhc = 1.0f, de_rlp[2];
EN_COLD static void syn_fx(int t)                /* a track's sends, its delay's colour and time; the plate */
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    const float dm = (float)E->syn[SY_DMIX] * 0.01f, rm = (float)E->syn[SY_RMIX] * 0.01f,
                tone = (float)E->syn[SY_DTONE] * 0.01f;
    const int tv = E->syn[SY_DTONE], bl = E->syn[SY_DBLUR];
    /* Mix: up to the middle the send rises to full, past it the dry falls away (two effects: the dry the less of both) */
    T->dmix = fm_minf(2.0f * dm, 1.0f);
    T->rmix = fm_minf(2.0f * rm, 1.0f);
    T->dry = fm_minf(2.0f * (1.0f - dm), 1.0f) * fm_minf(2.0f * (1.0f - rm), 1.0f);
    T->wow = (float)E->syn[SY_DWOW] * 0.01f;
    T->drive = (float)E->syn[SY_DDRIVE] * 0.01f;
    T->fb = (float)E->syn[SY_DFB] * 0.01f;       /* (Feedback in %: past 100 it swells) */
    T->hpc = 0.12f + 0.24f * tone * tone;        /* the repeats' band: ~220..800 Hz below, ~1.1..2.6 kHz above */
    T->lpc = 0.58f + 0.3f * tone;
    {   /* (towards 100% the band opens, wide by 100: what goes round is kept whole, and holds; below 85% the repeats
         * thin and darken as Tone has them) */
        const float k = fm_clampf((T->fb - 0.85f) * (1.0f / 0.15f), 0.0f, 1.0f);
        T->hpc += (0.012f - T->hpc) * k;
        T->lpc += (0.98f - T->lpc) * k;
    }
    /* what enters the tape: a low-pass ~2 kHz at 50, down to 500 Hz at 0, up to 5 kHz at 100 */
    T->in_c = 1.0f - fm_expf(-2.0f * FM_PI * fm_clampf(2000.0f * fm_exp2f((float)(tv - 50) * (tv > 50 ? 0.026439f : 0.04f)),
                                                      400.0f, 5000.0f) / TAPE_SR);
    T->blur = bl <= 50 ? 0.15f + 0.009f * (float)bl : 0.6f + 0.003f * (float)(bl - 50);   /* the diffusers' gain */
    T->wide = (float)E->syn[SY_DWIDTH] * 0.02f;  /* 0 mono, 1 (50), 2 wide */
    T->dmode = (uint8_t)E->syn[SY_DMODE];
    T->ping = (uint8_t)(E->syn[SY_DPING] != 0);
    {   /* the band-pass's loudest: a low-pass (lpc), less its own low-pass (hpc); looked for from 60 Hz to 5 kHz */
        const float a = 1.0f - T->lpc, a2 = 1.0f - T->hpc;
        float best = 1e-6f, hz;
        for (hz = 60.0f; hz < 5000.0f; hz *= 1.12f) {
            const float c = fm_cosf(2.0f * FM_PI * hz / TAPE_SR);
            const float m = T->lpc * T->lpc / (1.0f + a * a - 2.0f * a * c) * a2 * a2 * (2.0f - 2.0f * c) / (1.0f + a2 * a2 - 2.0f * a2 * c);
            if (m > best)
                best = m;
        }
        T->fbn = 1.0f / fm_sqrtf(best);
    }
    echo_time(t);
    pl_decay_t = 0.45f + 0.0051f * (float)E->syn[SY_RDECAY];   /* (to 0.96) */
    de_pdamp = 0.95f - 0.8f * (float)E->syn[SY_RDAMP] * 0.01f;
    de_pre = 1 + (int)((float)(PRE_N - 2) * (float)E->syn[SY_RPRE] * 0.01f);   /* (1 .. 100 ms) */
    de_rlow = (float)E->syn[SY_RLOW] * 0.01f * 0.11f;   /* (a one-pole high-pass: to ~800 Hz) */
    de_rwidth = (float)E->syn[SY_RWIDTH] * 0.02f;   /* (0 mono, 1 as the plate is, 2 wider) */
    de_rv_sz = 0.35f + 0.0065f * (float)E->syn[SY_RSIZE];   /* (the plate's lines read shorter: a smaller room) */
    de_rv_diff = (float)E->syn[SY_RDIFF] * 0.0125f;   /* (0 .. 1.25 of the plate's own diffusion) */
    de_rv_md = (float)E->syn[SY_RMODD] * 0.01f;
    de_rv_mr = 0.00002f * fm_exp2f((float)E->syn[SY_RMODR] * 0.05f);   /* (~0.07 .. 2.3 Hz at half the rate) */
    de_rhc = 1.0f - fm_expf(-2.0f * FM_PI * (1000.0f * fm_exp2f((float)E->syn[SY_RHICUT] * 0.0432f)) / EN_SR);   /* (1 .. 20 kHz) */
    de_rfreeze = (uint8_t)(E->syn[SY_RFREEZE] != 0);
}
EN_COLD static void syn_defaults(int t)
{
    int i;
    for (i = 0; i < SY_N; i++)
        en_trk[t].syn[i] = SYN_INFO[i].def;
    tk[t].ready = 1;
    syn_fx(t);
}
static int syn_shared(int p)                     /* the plate's own: one for both tracks */
{
    return (p >= SY_RDECAY && p <= SY_RFREEZE) && p != SY_RMIX;
}
EN_COLD static void syn_cmd(int t, int p, int v)
{
    int u;
    if ((unsigned)p >= SY_N || t >= NC)
        return;
    for (u = 0; u < NC; u++)
        if (!tk[u].ready)
            syn_defaults(u);
    v = (int16_t)v;                              /* (posted as 16 bits: a negative depth back) */
    v = v < SYN_INFO[p].lo ? SYN_INFO[p].lo : v > SYN_INFO[p].hi ? SYN_INFO[p].hi : v;
    if (syn_shared(p))
        for (u = 0; u < NC; u++)
            en_trk[u].syn[p] = (int16_t)v;
    else
        en_trk[t].syn[p] = (int16_t)v;
    syn_fx(t);
}
EN_COLD static void syn_trig(int t)              /* a chord struck: every envelope from where it is, to its attack */
{
    int i;
    if (!tk[t].ready)
        syn_defaults(t);
    for (i = 0; i < NENV; i++)
        tk[t].env[i].st = ST_ATT, en_trk[t].env_age[i] = 0.0f;
    tk[t].rnd[0] = tk[t].rnd[1] = 0.0f;
    tk[t].dk = 0.9f;                             /* (the repeats under the strike a moment) */
}
static float sy_env_time(int v, float mul)          /* 0..100 -> 1 ms .. 10 s (exponential), times a modulation */
{
    return 0.001f * fm_exp2f((float)v * 0.1329f) * mul;   /* (2^13.29 = 10000) */
}
static float sy_env_step(int t, int e, int a, int d, int s, int r, float am, float dm, float rm, int gate)   /* 32 samples on */
{
    volatile en_trk_t *E = &en_trk[t];
    float *lv = &tk[t].env[e].lv;
    uint8_t *st = &tk[t].env[e].st;
    const float sus = (float)s * 0.01f;
    if (!gate && *st != ST_IDLE && *st != ST_REL)
        *st = ST_REL, E->env_age[e] = 0.0f;
    E->env_age[e] += 32.0f / EN_SR;
    switch (*st) {
    case ST_ATT:                                 /* a straight line up */
        *lv += 32.0f / (sy_env_time(a, am) * EN_SR);
        if (*lv >= 1.0f)
            *lv = 1.0f, *st = ST_DEC, E->env_age[e] = 0.0f;
        break;
    case ST_DEC:                                 /* an exponential fall to the sustain (the time to ~-60 dB of it) */
        *lv = sus + (*lv - sus) * fm_expf(-32.0f * 6.9f / (sy_env_time(d, dm) * EN_SR));
        break;
    case ST_REL:
        *lv *= fm_expf(-32.0f * 6.9f / (sy_env_time(r, rm) * EN_SR));
        if (*lv < 1e-4f)
            *lv = 0.0f, *st = ST_IDLE;
        break;
    default:
        *lv = 0.0f;
        break;
    }
    E->env_lv[e] = *lv;
    E->env_st[e] = *st;
    return *lv;
}
static float sy_lfo_step(int t, int k)              /* LFO k, 32 samples on: -1 .. +1 */
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    const int rate = E->syn[k ? SY_L2RATE : SY_L1RATE], shape = E->syn[k ? SY_L2SHAPE : SY_L1SHAPE],
              sync = E->syn[k ? SY_L2SYNC : SY_L1SYNC];
    float hz, ph, y;
    if (sync > 0 && sync < EN_NSYNC)             /* in time: a cycle as many beats */
        hz = 1.0f / (SYNC_BEATS[sync] * quarter() * (1.0f / EN_SR));
    else
        hz = 0.02f * fm_exp2f((float)rate * 0.0997f);   /* 0.02 Hz .. 20 Hz */
    ph = T->lfo_ph[k] + hz * 32.0f * (1.0f / EN_SR);
    if (ph >= 1.0f) {
        ph -= (float)(int)ph;
        T->rnd[k] = 2.0f * frand() - 1.0f;       /* (the random: a new value each cycle) */
    }
    T->lfo_ph[k] = ph;
    switch (shape) {
    case EN_LS_TRI: y = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case EN_LS_SAW: y = 2.0f * ph - 1.0f; break;
    case EN_LS_SQUARE: y = ph < 0.5f ? 1.0f : -1.0f; break;
    case EN_LS_RANDOM: y = T->rnd[k]; break;
    default: y = sine(ph); break;
    }
    E->lfo_ph[k] = ph;
    E->lfo_val[k] = y;
    return y;
}
static inline float sy_osc(int wave, float ph, float dt)
{
    switch (wave) {
    case EN_WAVE_SINE: return sine(ph);
    case EN_WAVE_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case EN_WAVE_SQUARE: {
        float p2 = ph + 0.5f;
        if (p2 >= 1.0f)
            p2 -= 1.0f;
        return 0.5f * (blep_saw(ph, dt) - blep_saw(p2, dt));
    }
    default: return blep_saw(ph, dt);
    }
}
static int render_synth(int t, uint32_t n)       /* into tl, tr (the dry), es (the echo's send), rs (the plate's); 0: silent */
{
    int heard = 0;
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    uint32_t j;
    int i;
    float md[EN_NMD], f[EN_NPAD], dt[EN_NPAD][2], fold, fdrive, amp0, amp1, gain, echo, plate;
    float s0 = T->s[0], s1 = T->s[1], s2 = T->s[2], s3 = T->s[3];
    const int gate = T->key_held || T->sq_gate > 0.0f, wave = E->syn[SY_WAVE];
    if (!T->ready)
        syn_defaults(t);
    for (j = 0; j < n; j += 32u) {
        const uint32_t m = n - j < 32u ? n - j : 32u;
        float e1, e2, l1, l2, fe, cut, k, det, lvl;
        uint32_t u;
        /* the modulation: Env 1, Env 2, the LFOs, each to its target */
        for (i = 0; i < EN_NMD; i++)
            md[i] = 0.0f;
        e1 = sy_env_step(t, ENV_1, E->syn[SY_1A], E->syn[SY_1D], E->syn[SY_1S], E->syn[SY_1R], 1.0f, 1.0f, 1.0f, gate);
        e2 = sy_env_step(t, ENV_2, E->syn[SY_2A], E->syn[SY_2D], E->syn[SY_2S], E->syn[SY_2R], 1.0f, 1.0f, 1.0f, gate);
        l1 = sy_lfo_step(t, 0);
        l2 = sy_lfo_step(t, 1);
        md[E->syn[SY_1DST] % EN_NMD] += e1 * (float)E->syn[SY_1AMT] * 0.01f;
        md[E->syn[SY_2DST] % EN_NMD] += e2 * (float)E->syn[SY_2AMT] * 0.01f;
        md[E->syn[SY_L1DST] % EN_NMD] += l1 * (float)E->syn[SY_L1AMT] * 0.01f;
        md[E->syn[SY_L2DST] % EN_NMD] += l2 * (float)E->syn[SY_L2AMT] * 0.01f;
        /* the amp's envelope (its times moved by their targets: x16 .. /16) and the filter's */
        amp0 = T->amp_s;
        amp1 = sy_env_step(t, ENV_AMP, E->syn[SY_AA], E->syn[SY_AD], E->syn[SY_AS], E->syn[SY_AR], fm_exp2f(4.0f * md[EN_MD_AATT]),
                           fm_exp2f(4.0f * md[EN_MD_ADEC]), fm_exp2f(4.0f * md[EN_MD_AREL]), gate);
        fe = sy_env_step(t, ENV_FILT, E->syn[SY_FA], E->syn[SY_FD], E->syn[SY_FS], E->syn[SY_FR], 1.0f, 1.0f, 1.0f, gate);
        /* the oscillators: each voice's pitch (Pitch: +-12 semitones), its two apart by Detune (to +-25 cents) */
        det = fm_clampf((float)E->syn[SY_DETUNE] * 0.01f + md[EN_MD_DETUNE], 0.0f, 1.0f) * 0.0145f;
        for (i = 0; i < EN_NPAD; i++) {
            f[i] = mtof((float)T->notes[i] + 12.0f * md[EN_MD_PITCH]);
            dt[i][0] = f[i] * (1.0f - det) * (1.0f / EN_SR);
            dt[i][1] = f[i] * (1.0f + det) * (1.0f / EN_SR);
        }
        fold = fm_clampf((float)E->syn[SY_FOLD] * 0.01f + md[EN_MD_FOLD], 0.0f, 1.0f);
        fdrive = 0.0625f + 0.8125f * fold;      /* (turns of the fold's sine a unit of the two oscillators: at 0 an
                                                  * eighth of a turn at their peak, nearly straight; at 100 past two folds) */
        /* the filter: Cutoff 40 Hz .. 16 kHz, the envelope +-6 octaves, Key track from the chord's middle (C4), the
         * target's +-5 octaves */
        cut = 40.0f * fm_exp2f((float)(T->lk[EN_LK_CUTOFF] ? T->lk[EN_LK_CUTOFF] - 1 : E->syn[SY_CUT]) * 0.0864f + 6.0f * fe * (float)E->syn[SY_FENV] * 0.01f +
                               (float)E->syn[SY_KTRK] * 0.01f * ((float)T->notes[1] - 60.0f) * (1.0f / 12.0f) +
                               5.0f * md[EN_MD_CUTOFF]);
        k = 2.0f - 1.9f * fm_clampf((float)E->syn[SY_RESO] * 0.01f + md[EN_MD_RESO], 0.0f, 1.0f);
        svf_coefs(T->c, fm_clampf(cut, 30.0f, 16000.0f), k);
        if (!j) {                                /* (the screen's: as heard at the block's start) */
            for (i = 0; i < EN_NMD; i++)
                E->md_now[i] = md[i];
            E->cut_now = fm_clampf(cut, 30.0f, 16000.0f);
            E->fold_now = fold;
            E->det_now = det * (1.0f / 0.0145f);
        }
        lvl = fm_clampf((float)(T->lk[EN_LK_LEVEL] ? T->lk[EN_LK_LEVEL] - 1 : E->syn[SY_LEVEL]) * 0.01f * (1.0f + md[EN_MD_LEVEL]), 0.0f, 1.5f);
        gain = lvl * 0.28f;
        echo = fm_clampf((T->lk[EN_LK_SEND] ? fm_minf(0.02f * (float)(T->lk[EN_LK_SEND] - 1), 1.0f) : T->dmix) + md[EN_MD_ECHO], 0.0f, 1.0f) * 0.9f * 0.3025f;   /* (DUB-1's Echo at 55: what the tape takes) */
        plate = T->rmix * 0.6f;
        if (amp0 < 1e-5f && amp1 < 1e-5f) {     /* (silent: the oscillators run on, nothing heard) */
            for (i = 0; i < EN_NPAD; i++) {
                T->ph[i][0] += dt[i][0] * (float)m;
                T->ph[i][1] += dt[i][1] * (float)m;
                T->ph[i][0] -= (float)(int)T->ph[i][0];
                T->ph[i][1] -= (float)(int)T->ph[i][1];
            }
            T->amp_s = amp1;
            continue;
        }
        heard = 1;
        for (u = 0; u < m; u++) {
            const uint32_t jj = j + u;
            const float am = amp0 + (amp1 - amp0) * (float)u * (1.0f / 32.0f);
            float l = 0.0f, r = 0.0f, v1, v2, v3, yl, yr;
            for (i = 0; i < EN_NPAD; i++) {
                float x = sy_osc(wave, T->ph[i][0], dt[i][0]) + sy_osc(wave, T->ph[i][1], dt[i][1]);
                x = sine(x * fdrive + 4.0f);     /* the fold: driven past the edge, it folds back (a sine of it) */
                T->ph[i][0] += dt[i][0];
                if (T->ph[i][0] >= 1.0f)
                    T->ph[i][0] -= 1.0f;
                T->ph[i][1] += dt[i][1];
                if (T->ph[i][1] >= 1.0f)
                    T->ph[i][1] -= 1.0f;
                l += x * pan_l[i];
                r += x * pan_r[i];
            }
            v3 = l - s1;
            v1 = T->c[0] * s0 + T->c[1] * v3;
            v2 = s1 + T->c[1] * s0 + T->c[2] * v3;
            s0 = 2.0f * v1 - s0;
            s1 = 2.0f * v2 - s1;
            yl = v2;
            v3 = r - s3;
            v1 = T->c[0] * s2 + T->c[1] * v3;
            v2 = s3 + T->c[1] * s2 + T->c[2] * v3;
            s2 = 2.0f * v1 - s2;
            s3 = 2.0f * v2 - s3;
            yr = v2;
            yl = sat(yl * am * gain * 2.0f) * 0.5f;
            yr = sat(yr * am * gain * 2.0f) * 0.5f;
            tl[jj] += yl * T->dry;
            tr[jj] += yr * T->dry;
            es[jj] += (yl + yr) * echo;
            rs[jj] += (yl + yr) * plate;
        }
        T->amp_s = amp1;
    }
    T->s[0] = fm_flush(s0);
    T->s[1] = fm_flush(s1);
    T->s[2] = fm_flush(s2);
    T->s[3] = fm_flush(s3);
    E->syn_amp = T->amp_s;
    return heard;
}

/* ------------------------------------------------ the level and the EQ --- */
/* A track's eight bands, biquads (the Audio EQ Cookbook's, R. Bristow-Johnson): a band flat or off is not run. */
EN_COLD int en_mix_lo(int p) { return p >= MX_EQ && (p - MX_EQ) % EQ_NF == EQ_GAIN ? -15 : 0; }
EN_COLD int en_mix_hi(int p) { return p >= MX_EQ && (p - MX_EQ) % EQ_NF == EQ_GAIN ? 15 : 100; }
EN_COLD int en_mix_def(int p)
{
    static const uint8_t F[EN_NEQ] = {0, 23, 37, 51, 67, 80, 87, 100};   /* off, 98 Hz, 258, 678, 2 k, 5 k, 8.2 k, off */
    if (p == MX_LEVEL)
        return 80;
    p -= MX_EQ;
    return p % EQ_NF == EQ_FREQ ? F[p / EQ_NF % EN_NEQ] : p % EQ_NF == EQ_Q ? 35 : 0;
}
EN_COLD float en_eq_hz(int v) { return 20.0f * fm_exp2f((float)v * 0.0996578f); }   /* 20 Hz .. 20 kHz */
EN_COLD static void eq_coefs(int t, int b)
{
    volatile en_trk_t *E = &en_trk[t];
    const int fv = E->mx[MX_EQ + b * EQ_NF + EQ_FREQ], gv = E->mx[MX_EQ + b * EQ_NF + EQ_GAIN], qv = E->mx[MX_EQ + b * EQ_NF + EQ_Q];
    const float w = 2.0f * FM_PI * fm_clampf(en_eq_hz(fv), 20.0f, 20000.0f) * (1.0f / EN_SR), cs = fm_cosf(w), sn = fm_sinf(w),
                A = fm_exp2f((float)gv * 0.0830482f);   /* 10^(dB / 40) */
    float b0, b1, b2, a0, a1, a2;
    int on;
    if (b == EN_EQ_LOCUT || b == EN_EQ_HICUT) {
        const float al = sn * 0.70710678f, k = b == EN_EQ_LOCUT ? 1.0f + cs : 1.0f - cs;
        on = b == EN_EQ_LOCUT ? fv > 0 : fv < 100;
        b0 = b2 = 0.5f * k, b1 = b == EN_EQ_LOCUT ? -k : k;
        a0 = 1.0f + al, a1 = -2.0f * cs, a2 = 1.0f - al;
    } else if (b == EN_EQ_LOSHELF || b == EN_EQ_HISHELF) {
        const float sq = 2.0f * fm_sqrtf(A) * sn * 0.70710678f, s = b == EN_EQ_LOSHELF ? 1.0f : -1.0f;
        on = gv != 0;
        b0 = A * ((A + 1.0f) - s * (A - 1.0f) * cs + sq);
        b1 = s * 2.0f * A * ((A - 1.0f) - s * (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) - s * (A - 1.0f) * cs - sq);
        a0 = (A + 1.0f) + s * (A - 1.0f) * cs + sq;
        a1 = -s * 2.0f * ((A - 1.0f) + s * (A + 1.0f) * cs);
        a2 = (A + 1.0f) + s * (A - 1.0f) * cs - sq;
    } else {
        const float al = sn / (2.0f * 0.35f * fm_exp2f((float)qv * 0.045f));   /* Q 0.35 .. 8 */
        on = gv != 0;
        b0 = 1.0f + al * A, b1 = -2.0f * cs, b2 = 1.0f - al * A;
        a0 = 1.0f + al / A, a1 = -2.0f * cs, a2 = 1.0f - al / A;
    }
    a0 = 1.0f / a0;
    tk[t].eq[b].b0 = b0 * a0, tk[t].eq[b].b1 = b1 * a0, tk[t].eq[b].b2 = b2 * a0;
    tk[t].eq[b].a1 = a1 * a0, tk[t].eq[b].a2 = a2 * a0;
    if (!on)
        tk[t].eqz[b][0] = tk[t].eqz[b][1] = tk[t].eqz[b][2] = tk[t].eqz[b][3] = 0.0f;
    tk[t].eq[b].on = (uint8_t)on;
}
EN_COLD float en_eq_db(float hz)                 /* (the bands' analogue curves: the picture's, steady at any frequency) */
{
    volatile en_trk_t *E = &en_trk[en_sel < NT ? en_sel : 0];
    float db = 0.0f;
    int b;
    for (b = 0; b < EN_NEQ; b++) {
        const int fv = E->mx[MX_EQ + b * EQ_NF + EQ_FREQ], gv = E->mx[MX_EQ + b * EQ_NF + EQ_GAIN], qv = E->mx[MX_EQ + b * EQ_NF + EQ_Q];
        const float x = fm_maxf(hz, 1.0f) / en_eq_hz(fv), x2 = x * x, A = fm_exp2f((float)gv * 0.0830482f);
        float num, den;
        if (b == EN_EQ_LOCUT || b == EN_EQ_HICUT) {
            if (b == EN_EQ_LOCUT ? fv <= 0 : fv >= 100)
                continue;
            num = b == EN_EQ_LOCUT ? x2 * x2 : 1.0f, den = 1.0f + x2 * x2;
        } else if (!gv) {
            continue;
        } else if (b == EN_EQ_LOSHELF || b == EN_EQ_HISHELF) {
            const float m = 2.0f * A * x2, p = (A - x2) * (A - x2) + m, q = (1.0f - A * x2) * (1.0f - A * x2) + m;
            num = A * A * (b == EN_EQ_LOSHELF ? p : q), den = b == EN_EQ_LOSHELF ? q : p;
        } else {
            const float Q = 0.35f * fm_exp2f((float)qv * 0.045f), d = (1.0f - x2) * (1.0f - x2);
            num = d + (A * x / Q) * (A * x / Q), den = d + (x / (A * Q)) * (x / (A * Q));
        }
        db += 3.0103f * fm_log2f(fm_maxf(num, 1e-12f) / fm_maxf(den, 1e-12f));
    }
    return db;
}
EN_COLD static void mix_cmd(int t, int p, int v)
{
    if ((unsigned)p >= MX_N || t >= NC)
        return;
    v = (int16_t)v;
    v = v < en_mix_lo(p) ? en_mix_lo(p) : v > en_mix_hi(p) ? en_mix_hi(p) : v;
    en_trk[t].mx[p] = (int16_t)v;
    if (p == MX_LEVEL)
        tk[t].gain = (float)(v * v) * (1.0f / 6400.0f);   /* (80: as it is; 100: +3.9 dB) */
    else
        eq_coefs(t, (p - MX_EQ) / EQ_NF);
}
static void eq_run(tk_t *T, uint32_t n)          /* the track's bands over tl, tr */
{
    uint32_t j;
    int b;
    for (b = 0; b < EN_NEQ; b++) {
        const float b0 = T->eq[b].b0, b1 = T->eq[b].b1, b2 = T->eq[b].b2, a1 = T->eq[b].a1, a2 = T->eq[b].a2;
        float z0 = T->eqz[b][0], z1 = T->eqz[b][1], z2 = T->eqz[b][2], z3 = T->eqz[b][3];
        if (!T->eq[b].on)
            continue;
        for (j = 0; j < n; j++) {
            float x = tl[j], y = b0 * x + z0;
            z0 = b1 * x - a1 * y + z1;
            z1 = b2 * x - a2 * y;
            tl[j] = y;
            x = tr[j], y = b0 * x + z2;
            z2 = b1 * x - a1 * y + z3;
            z3 = b2 * x - a2 * y;
            tr[j] = y;
        }
        T->eqz[b][0] = fm_flush(z0), T->eqz[b][1] = fm_flush(z1), T->eqz[b][2] = fm_flush(z2), T->eqz[b][3] = fm_flush(z3);
    }
}

/* ------------------------------------------------- the keys, the pattern --- */
/* The keys play chords: a key's note is the root of the triad the track's scale builds on it (the scale's degrees two
 * and four above), voiced as asked, then the whole an octave down (keys F3..G5: the chords F2 up). The fourth voice
 * doubles the top note. A key outside the scale: none. */
const char *const EN_DIV_NAME[EN_NDIV] = {"1/8", "1/16", "1/32", "1/8T", "1/16T"};
EN_COLD static int scale_list(int t, int *s)       /* the scale's notes, semitones above the root, low to high */
{
    const uint32_t pool = en_trk[t].pool;
    int i, m = 0;
    for (i = 0; i < 12; i++)
        if (pool >> i & 1u)
            s[m++] = i;
    return m;
}
EN_COLD static int chord_voice(int t, int note, int voicing)   /* the triad on note, voiced, struck: 0 if not in the scale */
{
    volatile en_trk_t *E = &en_trk[t];
    int s[12], m = scale_list(t, s), rel, d, i, c[3], v[3];
    rel = ((note - E->root) % 12 + 12) % 12;
    for (d = 0; d < m && s[d] != rel; d++)
        ;
    if (d == m || m < 3)                         /* (not in the scale) */
        return 0;
    for (i = 0; i < 3; i++) {                    /* the degree, two above, four above: semitones above the key */
        const int k = d + 2 * i;
        c[i] = s[k % m] + 12 * (k / m) - s[d];
    }
    switch (voicing) {
    case EN_VO_ROOT: v[0] = c[0]; v[1] = c[1]; v[2] = c[2]; break;
    case EN_VO_SECOND: v[0] = c[2]; v[1] = c[0] + 12; v[2] = c[1] + 12; break;
    default: v[0] = c[1]; v[1] = c[2]; v[2] = c[0] + 12; break;
    }
    for (i = 0; i < 3; i++) {
        tk[t].notes[i] = (uint8_t)(note - 12 + v[i]);
        E->key_tri[i] = tk[t].notes[i];
    }
    tk[t].notes[3] = tk[t].notes[2];
    E->now_pc = (uint8_t)(((note % 12) + 12) % 12);
    E->now_q = (uint8_t)(c[1] == 4 && c[2] == 7 ? EN_Q_MAJ : c[1] == 3 && c[2] == 7 ? EN_Q_MIN : c[1] == 3 && c[2] == 6 ? EN_Q_DIM
                         : c[1] == 4 && c[2] == 8 ? EN_Q_AUG : EN_Q_OTHER);
    E->now_n++;
    syn_trig(t);
    return 1;
}
static void voice_on(int t, int note, float vel);
EN_COLD static int strike(int t, int note, int voicing)   /* a track's note: A, B its chord; the bass, the ping the note
                                                  * itself (two octaves under, an octave over); 0 if not in the scale */
{
    volatile en_trk_t *E = &en_trk[t];
    if (t < NC)
        return chord_voice(t, note, voicing);
    if (!(E->pool >> (((note - E->root) % 12 + 12) % 12) & 1u))
        return 0;
    E->key_tri[0] = E->key_tri[1] = E->key_tri[2] = (uint8_t)note;
    E->now_pc = (uint8_t)(((note % 12) + 12) % 12);
    E->now_q = EN_Q_MAJ;
    E->now_n++;
    voice_on(t, note + (t == VT_BASS ? -24 : 12), 1.0f);
    return 1;
}
static int base_note(int t) { return 60 + en_trk[t].root; }   /* the root's octave: C4 .. B4 */
EN_COLD static int seq_note(int t, const en_step_t *st)
{
    int s[12], m = scale_list(t, s);
    if (!m)
        return base_note(t);
    return base_note(t) + s[st->deg % m] + 12 * ((int)st->oct - 2);
}
EN_COLD static int note_step(int t, int note, en_step_t *st)
{
    int s[12], m = scale_list(t, s), rel = note - base_note(t), o, r, d;
    o = rel >= 0 ? rel / 12 : -((11 - rel) / 12);
    r = rel - 12 * o;
    for (d = 0; d < m && s[d] != r; d++)
        ;
    if (d == m)
        return 0;
    if (o < -2)
        o = -2;
    if (o > 1)
        o = 1;
    st->deg = (uint8_t)d;
    st->oct = (uint8_t)(o + 2);
    return 1;
}
EN_COLD int en_seq_note(const en_step_t *st) { return seq_note(en_sel < NT ? en_sel : 0, st); }
EN_COLD int en_note_step(int note, en_step_t *st) { return note_step(en_sel < NT ? en_sel : 0, note, st); }

EN_COLD static void seq_write(int t, int at, int len, const en_step_t *st)   /* a take: at its step, as long, the steps
                                                  * it covers emptied (it replaces them) */
{
    volatile en_trk_t *E = &en_trk[t];
    int i;
    for (i = 1; i < len && i < E->seq_len; i++)
        E->seq[(at + i) % E->seq_len].on = 0;
    E->seq[at].on = 1;
    E->seq[at].deg = st->deg;
    E->seq[at].oct = st->oct;
    E->seq[at].voi = st->voi;
    E->seq[at].len = (uint8_t)len;
    E->seq[at].lock[0] = E->seq[at].lock[1] = E->seq[at].lock[2] = 0;
}
EN_COLD static void key_chord(int t, int note, int on)
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    if (!on) {
        if (T->key_held && note == E->key_note) {
            T->key_held = 0, E->key_note = 0;
            if (T->rec_step >= 0) {              /* (a take: as long as it was held, in steps, at least one) */
                const float st = step_samples(t);
                int len = (int)((float)(sq_t - T->rec_t0) / st + 0.5f);
                en_step_t s;
                s.deg = E->seq[T->rec_step].deg, s.oct = E->seq[T->rec_step].oct, s.voi = EN_VO_KEY;
                len = len < 1 ? 1 : len > E->seq_len ? E->seq_len : len;
                seq_write(t, T->rec_step, len, &s);
                T->rec_step = -1;
            }
        }
        return;
    }
    if (!strike(t, note, E->voicing))
        return;
    T->key_held = 1;
    T->lk[0] = T->lk[1] = T->lk[2] = 0;          /* (a key: no step's locks) */
    T->sq_gate = 0.0f;                           /* (a key over the pattern's chord: the key's now) */
    E->key_note = (uint8_t)note;
    if (en_seq_playing && en_seq_rec) {          /* recording: on its nearest step, as long as held */
        en_step_t s;
        const float st = step_samples(t);
        int at = E->seq_pos;
        if (T->sq_left < 0.5f * st)              /* (past the step's middle: the next one) */
            at = (at + 1) % E->seq_len;
        if (note_step(t, note, &s)) {
            s.voi = EN_VO_KEY;
            seq_write(t, at, 1, &s);
            T->rec_step = at;
            T->rec_t0 = sq_t;
        }
    }
}
EN_COLD static void seq_fire(int t)              /* a step begins: its chord, if it has one */
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    volatile en_step_t *p = &E->seq[E->seq_pos];
    E->seq_steps++;
    if (!p->on || E->seq_pos == T->rec_step)     /* (the take's own step: the key already sounds it) */
        return;
    if (T->key_held && !en_seq_rec)              /* (a key held, not recording: the key's chord stays) */
        return;
    {
        en_step_t s;
        s.deg = p->deg, s.oct = p->oct, s.voi = p->voi, s.len = p->len;
        if (strike(t, seq_note(t, &s), s.voi == EN_VO_KEY ? E->voicing : s.voi)) {
            T->sq_gate = (float)s.len * step_samples(t) * 0.92f;
            T->lk[0] = p->lock[0], T->lk[1] = p->lock[1], T->lk[2] = p->lock[2];   /* (its locks, while it sounds) */
        }
    }
}
EN_COLD static void seq_cmd(int t, int op, int v)
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    int i, u;
    switch (op) {
    case SQ_PLAY:
        if (v && !en_seq_playing) {
            en_seq_playing = 1;
            for (u = 0; u < NT; u++) {           /* (the first step fires at once: from the last, one on) */
                en_trk[u].seq_pos = (uint8_t)(en_trk[u].seq_len - 1);
                en_trk[u].seq_steps = 0;
                tk[u].sq_left = 0.0f;
            }
            sq_beat_left = 0.0f;
            sq_beat = 0;
        } else if (!v && en_seq_playing) {
            en_seq_playing = 0;
            for (u = 0; u < NT; u++)
                tk[u].sq_gate = 0.0f, tk[u].rec_step = -1;
        }
        break;
    case SQ_REC:
        en_seq_rec = (uint8_t)(v != 0);
        if (!v)
            for (u = 0; u < NT; u++)
                tk[u].rec_step = -1;
        break;
    case SQ_LEN:
        E->seq_len = (uint8_t)(v < 1 ? 1 : v > EN_SEQ_MAX ? EN_SEQ_MAX : v);
        if (E->seq_pos >= E->seq_len)
            E->seq_pos = (uint8_t)(E->seq_len - 1);
        break;
    case SQ_DIV:
        E->seq_div = (uint8_t)(v < EN_NDIV ? v : EN_DIV_16);
        break;
    case SQ_CLEAR:
        for (i = 0; i < EN_SEQ_MAX; i++) {
            T->kept[i].on = E->seq[i].on, T->kept[i].deg = E->seq[i].deg, T->kept[i].oct = E->seq[i].oct;
            T->kept[i].voi = E->seq[i].voi, T->kept[i].len = E->seq[i].len;
            T->kept[i].lock[0] = E->seq[i].lock[0], T->kept[i].lock[1] = E->seq[i].lock[1], T->kept[i].lock[2] = E->seq[i].lock[2];
            E->seq[i].on = 0;
        }
        T->rec_step = -1;
        break;
    case SQ_RESTORE:
        for (i = 0; i < EN_SEQ_MAX; i++) {
            E->seq[i].on = T->kept[i].on, E->seq[i].deg = T->kept[i].deg, E->seq[i].oct = T->kept[i].oct;
            E->seq[i].voi = T->kept[i].voi, E->seq[i].len = T->kept[i].len;
            E->seq[i].lock[0] = T->kept[i].lock[0], E->seq[i].lock[1] = T->kept[i].lock[1], E->seq[i].lock[2] = T->kept[i].lock[2];
        }
        break;
    case SQ_METRO:
        en_metro_on = (uint8_t)(v >> 8 & 1);
        en_metro_lvl = (uint8_t)(v & 0x7F);
        break;
    }
}
static uint32_t seq_wait(void)                   /* samples to the sequencer's next event (a step, a beat) */
{
    float w = sq_beat_left;
    int t;
    if (!en_seq_playing)
        return 0xFFFFFFFFu;
    for (t = 0; t < NT; t++)
        if (tk[t].sq_left < w)
            w = tk[t].sq_left;
    return w <= 0.0f ? 0u : (uint32_t)w + 1u;
}
static void seq_events(void)                     /* a step or a beat due now: fire it */
{
    int t;
    if (!en_seq_playing)
        return;
    for (t = 0; t < NT; t++)
        if (tk[t].sq_left <= 0.0f) {
            en_trk[t].seq_pos = (uint8_t)((en_trk[t].seq_pos + 1u) % en_trk[t].seq_len);
            seq_fire(t);
            tk[t].sq_left += step_samples(t);
        }
    if (sq_beat_left <= 0.0f) {
        if (en_metro_on) {                       /* the click: the bar's first a fifth up, louder */
            const int one = !(sq_beat & 3u);
            ck_inc = (one ? 1760.0f : 1320.0f) * (1.0f / EN_SR);
            ck_ph = 0.0f;
            ck_amp = (one ? 0.5f : 0.3f) * (float)en_metro_lvl * 0.01f;
        }
        sq_beat++;
        sq_beat_left += quarter();
    }
}
static void seq_advance(int32_t *out, uint32_t k, float gain)   /* k samples passed: the click into them, the clocks on */
{
    uint32_t j;
    int t;
    sq_t += k;
    if (!en_seq_playing)
        return;
    sq_beat_left -= (float)k;
    for (t = 0; t < NT; t++) {
        tk[t].sq_left -= (float)k;
        if (tk[t].sq_gate > 0.0f && (tk[t].sq_gate -= (float)k) <= 0.0f)
            tk[t].sq_gate = 0.0f;
    }
    if (ck_amp > 1e-4f) {
        for (j = 0; j < k; j++) {
            const float c = sine(ck_ph) * ck_amp * gain;
            ck_ph += ck_inc;
            if (ck_ph >= 1.0f)
                ck_ph -= 1.0f;
            ck_amp *= 0.9985f;                   /* (~15 ms) */
            out[2 * j] += (int32_t)(c * 8300000.0f);
            out[2 * j + 1] += (int32_t)(c * 8300000.0f);
        }
        if (ck_amp <= 1e-4f)
            ck_amp = 0.0f;
    }
}

/* ------------------------------------------------- the bass, the pings --- */
/* DUB-1's FM voices (four operators), each track's sound its own: any of the eight patches, the FM's amount, the
 * modulator's ratio, op 4's feedback; an attack, a decay; a low-pass, a drive. The ping's delay: a small tape of its
 * own (1 s at a quarter rate), darker each time round. */
const char *const EN_VOICE_NAME[EN_NVOICE] = {"Sub", "Punch", "Rubber", "Pluck", "Growl", "Keys", "Bell", "Glass"};
const char *const EN_RATIO_NAME[EN_NRATIO] = {"x0.5", "x1", "x1.5", "x2", "x3", "x4", "x5", "x7"};
static const float RATIO_MUL[EN_NRATIO] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f};
static const fmpatch_t FM_SUB = {FM_ALG2, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.08f, 0.3f, 0.3f}, {2.0f, 0.1f, 0.1f, 0.0f},
                                                 {1.0f, 0.1f, 0.1f, 0.0f}}};
static const fmpatch_t FM_GLASS = {4, 0.0f, {{1.0f, 1.0f, 0.0f, 1.0f}, {5.07f, 0.32f, 0.3f, 0.05f}, {3.0f, 0.35f, 0.0f, 1.0f},
                                             {9.1f, 0.12f, 0.1f, 0.0f}}};
static const fmpatch_t *const VPATCH[EN_NVOICE] = {&FM_SUB, &FM_PUNCH, &FM_RUBBER, &FM_PLUCK, &FM_GROWL, &FM_KEYS, &FM_BELL,
                                                   &FM_GLASS};
const en_vp_info_t VP_INFO[VP_N] = {
    {"Voice", 0, EN_NVOICE - 1, {2, 6}}, {"FM", 0, 100, {50, 50}}, {"Ratio", 0, EN_NRATIO - 1, {1, 1}}, {"Feedback", 0, 100, {0, 0}},
    {"Attack", 0, 100, {0, 0}}, {"Decay", 0, 100, {100, 45}}, {"Bite", 0, 100, {50, 50}}, {"Glide", 0, 100, {0, 60}},
    {"Cutoff", 0, 100, {100, 100}}, {"Drive", 0, 100, {0, 0}}, {"Level", 0, 100, {70, 70}}, {"Reverb", 0, 100, {0, 35}},
    {"Time", 0, EN_NDTIME - 1, {3, 3}}, {"Feedback", 0, 100, {0, 50}}, {"Mix", 0, 100, {0, 40}}, {"Tone", 0, 100, {50, 50}},
};
typedef struct {
    fmvoice_t fm;
    float env, vel, f, f_t, gl, gr;              /* the amp; the pitch now and wanted (a glide); the pan */
    uint8_t st;                                  /* 0 off, 1 attack, 2 decay, 3 release */
} fv_t;
#define NPING 6
static fv_t bassv, pingv[NPING];
static uint8_t ping_next;
static int32_t bass_api;                         /* (en_bass_note's: samples left of it) */
static float vf_s[2][4], vf_c[4];                /* the low-passes' states (the bass's, the ping's left and right) */
static float vl[BLK], vr[BLK];
#define PD_N 11025
static int16_t pd_buf[PD_N];
static int pd_w, pd_ph;
static int32_t pd_quiet = PD_N + 1;
static float pd_acc, pd_lp, pd_p, pd_c;

EN_COLD static void vp_cmd(int t, int p, int v)
{
    if ((unsigned)p >= VP_N || t < NC)
        return;
    v = (int16_t)v;
    en_trk[t].vp[p] = (int16_t)(v < VP_INFO[p].lo ? VP_INFO[p].lo : v > VP_INFO[p].hi ? VP_INFO[p].hi : v);
}
static const fmpatch_t *vpatch(int t) { return VPATCH[(unsigned)en_trk[t].vp[VP_VOICE] % EN_NVOICE]; }
static float vratio(int t) { return RATIO_MUL[(unsigned)en_trk[t].vp[VP_RATIO] % EN_NRATIO]; }
static float vfb(int t, const fmpatch_t *p) { return fm_clampf(p->fb + 0.007f * (float)en_trk[t].vp[VP_FB], 0.0f, 0.7f); }
EN_COLD static void voice_on(int t, int note, float vel)
{
    volatile en_trk_t *E = &en_trk[t];
    const float f = mtof((float)note);
    fv_t *v;
    if (t == VT_BASS) {
        v = &bassv;
        v->f_t = f;
        if (!v->st || E->vp[VP_GLIDE] <= 0)      /* (a note over another, Glide up: it slides there) */
            v->f = f;
    } else {
        const int k = ping_next;
        const float pos = (0.5f + (k & 1 ? 0.4f : -0.4f) * (float)E->vp[VP_GLIDE] * 0.01f) * (FM_PI / 2.0f);   /* (Spread) */
        ping_next = (uint8_t)((k + 1) % NPING);
        v = &pingv[k];
        v->f = v->f_t = f;
        v->gl = fm_cosf(pos);
        v->gr = fm_sinf(pos);
    }
    fm_note(&v->fm, vpatch(t), v->f, vratio(t), 1.0f, fm_exp2f((float)(E->vp[VP_MDEC] - 50) * 0.06f));
    v->vel = vel;
    v->st = 1;
}
EN_COLD void en_voice_wave(float *w, int n)
{
    const int t = en_sel;
    fmvoice_t fm = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}, 0.0f, 0.0f};
    int i;
    if (t < NC || t >= NT || n < 2) {
        for (i = 0; i < n; i++)
            w[i] = 0.0f;
        return;
    }
    fm_note(&fm, vpatch(t), 2.0f * EN_SR / (float)n, vratio(t), 1.0f, 1.0f);
    fm_ctl(&fm, vpatch(t), vpatch(t)->alg, (float)en_trk[t].vp[VP_FM] * 0.02f);
    for (i = 0; i < n; i++)
        w[i] = fm_clampf(fm_sample(&fm, vpatch(t)->alg, vfb(t, vpatch(t)), 1.0f) * 0.65f, -1.0f, 1.0f);
}
static void voice_run(int t, fv_t *v, uint32_t n, int gate)   /* a voice, n samples on: into vl (the ping: vl and vr) */
{
    volatile en_trk_t *E = &en_trk[t];
    const fmpatch_t *p = vpatch(t);
    const int alg = p->alg, bass = t == VT_BASS;
    const float ms = (float)E->vp[VP_FM] * 0.02f, fb = vfb(t, p),
                att = 1.0f / (0.0005f * fm_exp2f((float)E->vp[VP_ATT] * 0.0864f) * EN_SR),   /* 0.5 .. 200 ms */
                dk = bass && E->vp[VP_DEC] >= 100 ? 1.0f   /* (the bass at 100: held) */
                     : fm_expf(-6.9f / (0.05f * fm_exp2f((float)E->vp[VP_DEC] * 0.0664f) * EN_SR)),   /* 50 ms .. 5 s */
                rk = fm_expf(-6.9f / (0.04f * EN_SR));
    uint32_t j, u;
    for (j = 0; j < n && v->st; j += 32u) {
        const uint32_t m = n - j < 32u ? n - j : 32u;
        if (bass && v->f != v->f_t) {            /* the glide: to 300 ms */
            const float tau = (float)E->vp[VP_GLIDE] * 0.003f * EN_SR;
            v->f += (v->f_t - v->f) * (tau > (float)m ? (float)m / tau : 1.0f);
            if (fm_fabsf(v->f_t - v->f) < 0.02f)
                v->f = v->f_t;
            fm_tune(&v->fm, p, v->f, vratio(t), 1.0f);
        }
        fm_ctl(&v->fm, p, alg, ms);
        for (u = 0; u < m; u++) {
            float x;
            if (v->st == 1) {
                if ((v->env += att) >= 1.0f)
                    v->env = 1.0f, v->st = 2;
            } else if (v->st == 2) {
                if (bass && !gate)
                    v->st = 3;
                else
                    v->env *= dk;
            } else {
                v->env *= rk;
            }
            x = fm_sample(&v->fm, alg, fb, 1.0f) * v->env * v->vel;
            if (bass) {
                vl[j + u] += x;
            } else {
                vl[j + u] += x * v->gl;
                vr[j + u] += x * v->gr;
            }
        }
        if (v->st >= 2 && v->env < 1e-4f)
            v->st = 0, v->env = 0.0f;
    }
}
static inline float vlp(float *z, float x)       /* (the low-pass: a TPT SVF step, vf_c's) */
{
    const float v3 = x - z[1], v1 = vf_c[0] * z[0] + vf_c[1] * v3, v2 = z[1] + vf_c[1] * z[0] + vf_c[2] * v3;
    z[0] = 2.0f * v1 - z[0];
    z[1] = 2.0f * v2 - z[1];
    return v2;
}
static void voice_mix(int t, uint32_t n, float *pk)   /* vl (and vr): the drive, the low-pass, the level; into the mix */
{
    volatile en_trk_t *E = &en_trk[t];
    const tk_t *T = &tk[t];
    const int ping = t == VT_PING, lv = T->lk[EN_LK_LEVEL] ? T->lk[EN_LK_LEVEL] - 1 : E->vp[VP_LEVEL],
              cv = T->lk[EN_LK_CUTOFF] ? T->lk[EN_LK_CUTOFF] - 1 : E->vp[VP_CUT];
    const float g = (float)(lv * lv) * (1.0f / 4900.0f) * (ping ? 0.22f : 0.3f), d = (float)E->vp[VP_DRIVE] * 0.01f,
                dg = 1.0f + 7.0f * d, dn = (1.0f + 0.5f * d) / dg, rev = (float)E->vp[VP_REV] * 0.011f,
                dmix = ping ? (float)(T->lk[EN_LK_SEND] ? T->lk[EN_LK_SEND] - 1 : E->vp[VP_DMIX]) * 0.01f : 0.0f,
                dfb = (float)E->vp[VP_DFB] * 0.0095f, dtc = 0.15f + 0.8f * (float)E->vp[VP_DTONE] * 0.01f;
    float *z = vf_s[ping], a = 0.0f;
    int dlen = (int)(DTIME_BEATS[(unsigned)E->vp[VP_DTIME] % EN_NDTIME] * 60.0f / (float)en_tempo * 11025.0f);
    uint32_t j;
    dlen = dlen < 1 ? 1 : dlen > PD_N - 2 ? PD_N - 2 : dlen;
    if (cv < 100)
        svf_coefs(vf_c, 40.0f * fm_exp2f((float)cv * 0.0864f), 1.2f);
    for (j = 0; j < n; j++) {
        float l = vl[j], r = ping ? vr[j] : 0.0f;
        if (d > 0.0f) {
            l = sat(l * dg) * dn;
            if (ping)
                r = sat(r * dg) * dn;
        }
        if (cv < 100) {
            l = vlp(z, l);
            if (ping)
                r = vlp(z + 2, r);
        }
        l *= g;
        r = ping ? r * g : l;
        rsend[j] += (l + r) * rev;
        if (ping) {                              /* its delay */
            float e;
            pd_acc += (l + r) * 0.5f * dmix;
            if (++pd_ph >= 4) {
                int i0 = pd_w - dlen;
                float rd, w;
                pd_ph = 0;
                rd = (float)pd_buf[i0 < 0 ? i0 + PD_N : i0] * (1.0f / 20000.0f);
                pd_lp += dtc * (rd - pd_lp);
                w = (pd_acc * 0.25f + 0.7f * sat(pd_lp * dfb * (1.0f / 0.7f))) * 20000.0f;
                w = fm_fabsf(w) < 0.75f ? 0.0f : w + (w < 0.0f ? -0.5f : 0.5f);
                if (++pd_w >= PD_N)
                    pd_w = 0;
                pd_buf[pd_w] = (int16_t)(w > 32767.0f ? 32767.0f : w < -32767.0f ? -32767.0f : w);
                pd_quiet = pd_buf[pd_w] ? 0 : pd_quiet + 1;
                pd_acc = 0.0f;
                pd_p = pd_c;
                pd_c = rd;
            }
            e = pd_p + (pd_c - pd_p) * (float)pd_ph * 0.25f;
            l += e;
            r += e;
            rsend[j] += e * 0.2f;
        }
        a = fm_maxf(a, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
        mixl[j] += l;
        mixr[j] += r;
    }
    z[0] = fm_flush(z[0]), z[1] = fm_flush(z[1]), z[2] = fm_flush(z[2]), z[3] = fm_flush(z[3]);
    pd_lp = fm_flush(pd_lp);
    pk[t] = a;
}
static void render_voices(uint32_t n, float *pk)  /* the bass and the pings, into mixl, mixr and rsend */
{
    uint32_t j;
    int k, any = 0;
    if (bassv.st) {
        const tk_t *T = &tk[VT_BASS];
        for (j = 0; j < n; j++)
            vl[j] = 0.0f;
        voice_run(VT_BASS, &bassv, n, T->key_held || T->sq_gate > 0.0f || bass_api > 0);
        voice_mix(VT_BASS, n, pk);
    }
    if (bass_api > 0)
        bass_api -= (int32_t)n;
    for (k = 0; k < NPING; k++)
        any |= pingv[k].st;
    if (any || pd_quiet <= PD_N) {
        for (j = 0; j < n; j++)
            vl[j] = vr[j] = 0.0f;
        for (k = 0; k < NPING; k++)
            if (pingv[k].st)
                voice_run(VT_PING, &pingv[k], n, 0);
        voice_mix(VT_PING, n, pk);
    }
}

/* --------------------------------------------------------------- render --- */
static float ms_lp1, ms_lp2;
static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    const float og = 0.75f * gain;
    float pk[EN_NMETER] = {0.0f};
    uint32_t j;
    int t;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = 0.0f;
    {
        PROF_B(p1);
        for (t = 0; t < NC; t++) {
            tk_t *T = &tk[t];
            float dk = T->dk, dd = T->dd;
            for (j = 0; j < n; j++)
                tl[j] = tr[j] = es[j] = rs[j] = 0.0f;
            /* (a track silent, its tape empty and its diffusers too: its echo not run. An unused track costs little) */
            if (!render_synth(t, n) && T->quiet > TAPE_N + 4 * EAP_FB) {
                T->gain_s = T->gain;
                continue;
            }
            for (j = 0; j < n; j++) {
                float el, er, ge;
                echo_step(T, tape[t], eap[t], es[j], &el, &er);
                dd += (dk - dd) * 0.05f;         /* (a strike: the repeats down by up to 6 dB, back over ~150 ms) */
                dk *= 0.99985f;
                ge = 1.2f * (1.0f - 0.5f * fm_minf(dd, 1.0f));
                el *= ge;
                er *= ge;
                rs[j] += (el + er) * 0.12f;      /* (a little of the repeats into the plate) */
                tl[j] += el;
                tr[j] += er;
            }
            eq_run(T, n);
            {   /* the level, eased over the chunk; the meter; into the mix and the plate */
                float g = T->gain_s;
                const float dg = (T->gain - g) / (float)n;
                for (j = 0; j < n; j++) {
                    const float l = tl[j] * g, r = tr[j] * g, a = fm_maxf(fm_fabsf(l), fm_fabsf(r));
                    if (a > pk[t])
                        pk[t] = a;
                    mixl[j] += l;
                    mixr[j] += r;
                    rsend[j] += rs[j] * g;
                    g += dg;
                }
                T->gain_s = T->gain;
            }
            T->tape_lp = fm_flush(T->tape_lp), T->tape_hp = fm_flush(T->tape_hp);
            T->in_lp = fm_flush(T->in_lp), T->in_hp = fm_flush(T->in_hp);
            T->dk = fm_flush(dk);
            T->dd = fm_flush(dd);
        }
        PROF_E(1, p1);
    }
    {
        PROF_B(p2);
        render_voices(n, pk);
        PROF_E(2, p2);
    }
    {
        PROF_B(p3);
        for (j = 0; j < n; j++) {
            float wl, wr, l, r, sd, rin = rsend[j] * 0.21f;    /* (what the plate takes: as loud as DUB-1's plate and cloud were together) */
            de_rhp += de_rlow * (rin - de_rhp);  /* the Reverb page's Low cut, on what goes in */
            rin -= de_rhp;
            if (de_rfreeze)                      /* (Freeze: nothing new in, the tail held) */
                rin = 0.0f;
            if ((pl_ph ^= 1u) != 0u) {           /* the plate, every other sample (its input the pair's mean, its
                                                  * output a line between) */
                pl_acc = rin;
                wl = pl_ol, wr = pl_or;
            } else {
                float nl, nr;
                reverb(0.5f * (pl_acc + rin), &nl, &nr);
                wl = 0.5f * (pl_ol + nl), wr = 0.5f * (pl_or + nr);
                pl_ol = nl, pl_or = nr;
            }
            {   /* the Reverb pages' High cut and Width */
                float m, sd2;
                de_rlp[0] += de_rhc * (wl - de_rlp[0]);
                de_rlp[1] += de_rhc * (wr - de_rlp[1]);
                m = 0.5f * (de_rlp[0] + de_rlp[1]), sd2 = 0.5f * (de_rlp[0] - de_rlp[1]) * de_rwidth;
                wl = m + sd2, wr = m - sd2;
            }
            l = mixl[j] + 1.1f * wl;
            r = mixr[j] + 1.1f * wr;
            sd = 0.5f * (l - r);                 /* mono below ~80 Hz: the side's lows taken out (two poles) */
            ms_lp1 += 0.0113f * (sd - ms_lp1);
            ms_lp2 += 0.0113f * (sd - ms_lp1 - ms_lp2);
            sd = ms_lp1 + ms_lp2;
            l = (l - sd) * og;
            r = (r + sd) * og;
            pk[NT] = fm_maxf(pk[NT], fm_maxf(fm_fabsf(l), fm_fabsf(r)));   /* (the meter: going into the soft clip) */
            out[2 * j] = (int32_t)(soft(l) * 8300000.0f);
            out[2 * j + 1] = (int32_t)(soft(r) * 8300000.0f);
        }
        PROF_E(3, p3);
    }
    for (t = 0; t < NT; t++)
        if (pk[t] * og > en_meter[t])
            en_meter[t] = pk[t] * og;
    if (pk[NT] > en_meter[NT])
        en_meter[NT] = pk[NT];
    ms_lp1 = fm_flush(ms_lp1);
    ms_lp2 = fm_flush(ms_lp2);
    pl_bw = fm_flush(pl_bw), pl_damp_l = fm_flush(pl_damp_l), pl_damp_r = fm_flush(pl_damp_r);
    pl_ol = fm_flush(pl_ol), pl_or = fm_flush(pl_or), pl_acc = fm_flush(pl_acc);
    de_rhp = fm_flush(de_rhp);
    de_rlp[0] = fm_flush(de_rlp[0]);
    de_rlp[1] = fm_flush(de_rlp[1]);
    en_pad_level = fm_minf(fm_maxf(tk[0].amp_s, tk[1].amp_s) * 1.2f, 1.0f);
}

EN_COLD static void trk_reset(int t)
{
    volatile en_trk_t *E = &en_trk[t];
    tk_t *T = &tk[t];
    int i;
    for (i = 0; i < EN_SEQ_MAX; i++)
        E->seq[i].on = 0, E->seq[i].len = 1, T->kept[i].on = 0, T->kept[i].len = 1,
        E->seq[i].lock[0] = E->seq[i].lock[1] = E->seq[i].lock[2] = 0;
    E->seq_len = 16, E->seq_div = EN_DIV_16, E->seq_pos = 0, E->seq_steps = 0;
    E->root = 5, E->mode = MODE_DORIAN, E->pool = EN_MODE_POOL[MODE_DORIAN], E->voicing = EN_VO_FIRST;
    E->key_note = 0, E->now_n = 0;
    T->key_held = 0, T->sq_gate = 0.0f, T->sq_left = 0.0f, T->rec_step = -1;
    for (i = 0; i < EN_NPAD; i++) {
        T->notes[i] = 60;
        T->ph[i][0] = 0.13f * (float)i;
        T->ph[i][1] = 0.37f + 0.21f * (float)i;
    }
    for (i = 0; i < NENV; i++)
        T->env[i].lv = 0.0f, T->env[i].st = ST_IDLE;
    T->amp_s = 0.0f;
    T->len = 0.0f;
    T->lk[0] = T->lk[1] = T->lk[2] = 0;
    for (i = 0; i < VP_N; i++)
        E->vp[i] = VP_INFO[i].def[t == VT_PING];
    if (t >= NC)                                 /* (the bass, the ping: no synth, no tape, no EQ) */
        return;
    syn_defaults(t);
    for (i = 0; i < MX_N; i++)
        mix_cmd(t, i, en_mix_def(i));
    T->gain_s = T->gain;
}
EN_COLD static void quiet(void)                  /* everything silent, now: the envelopes, the tapes, the plate */
{
    int t, i;
    for (t = 0; t < NT; t++) {
        tk_t *T = &tk[t];
        T->key_held = 0, T->sq_gate = 0.0f, en_trk[t].key_note = 0;
        for (i = 0; i < NENV; i++)
            T->env[i].lv = 0.0f, T->env[i].st = ST_IDLE;
        T->amp_s = 0.0f;
        if (t >= NC)
            continue;
        for (i = 0; i < TAPE_N; i++)
            tape[t][i] = 0;
        for (i = 0; i < EAP_FB; i++)
            eap[t][i] = 0;
        T->tape_lp = T->tape_hp = T->in_lp = T->in_hp = T->p1 = T->c1 = T->p3 = T->c3 = T->tape_acc = 0.0f;
        T->quiet = TAPE_N + 4 * EAP_FB + 1;
        T->s[0] = T->s[1] = T->s[2] = T->s[3] = 0.0f;
    }
    bassv.st = 0, bassv.env = 0.0f, bass_api = 0;
    for (i = 0; i < NPING; i++)
        pingv[i].st = 0, pingv[i].env = 0.0f;
    for (i = 0; i < PD_N; i++)
        pd_buf[i] = 0;
    pd_quiet = PD_N + 1, pd_acc = pd_lp = pd_p = pd_c = 0.0f;
    plate_init();
    pl_ol = pl_or = pl_acc = 0.0f;
    ck_amp = 0.0f;
}
static void drain(void)
{
    while (q_r != q_w) {
        const uint32_t w = q[q_r % QN];
        const int t = (int)(w >> 29), c = (int)(w >> 24 & 0x1Fu), a = (int)(w >> 16 & 0xFFu), b = (int)(w & 0xFFFFu);
        volatile en_trk_t *E = &en_trk[t];
        int u;
        switch (c) {
        case C_SET:
            if (a == P_TEMPO) {
                en_tempo = (uint8_t)(b < EN_TEMPO_MIN ? EN_TEMPO_MIN : b > EN_TEMPO_MAX ? EN_TEMPO_MAX : b);
                for (u = 0; u < NC; u++)
                    echo_time(u);
            } else if (a == P_ROOT) {
                E->root = (uint8_t)(b % 12);
            } else if (a == P_MODE) {
                E->mode = (uint8_t)(b < EN_NMODES ? b : MODE_AEOLIAN);
                E->pool = EN_MODE_POOL[E->mode];
            }
            break;
        case C_POOL: E->pool = (uint16_t)((b & 0xFFF) | 1); break;
        case C_PANIC:
            seq_cmd(0, SQ_PLAY, 0);
            quiet();
            break;
        case C_KEY: key_chord(t, b & 0x7F, a); break;
        case C_VOICING: E->voicing = (uint8_t)(a < EN_NVO ? a : EN_VO_FIRST); break;
        case C_SEQ: seq_cmd(t, a, b); break;
        case C_STEP: {
            volatile en_step_t *p = &E->seq[a & 0x3F];
            p->on = (uint8_t)(b & 1), p->deg = (uint8_t)(b >> 1 & 7), p->oct = (uint8_t)(b >> 4 & 3);
            p->voi = (uint8_t)(b >> 6 & 3), p->len = (uint8_t)((b >> 8 & 0x3F) + 1);
            if (!p->on)
                p->lock[0] = p->lock[1] = p->lock[2] = 0;
            break;
        }
        case C_SYN: syn_cmd(t, a, b); break;
        case C_MIX: mix_cmd(t, a, b); break;
        case C_LOCK:
            if ((b >> 8 & 3) < EN_NLK)
                E->seq[a & 0x3F].lock[b >> 8 & 3] = (uint8_t)(b & 0xFF);
            break;
        case C_BASS:
            voice_on(VT_BASS, b & 0x7F, (float)(a ? a : 1) * (1.0f / 127.0f));
            bass_api = (int32_t)((float)((b >> 7) * 20) * 0.001f * EN_SR);
            break;
        case C_PING: voice_on(VT_PING, b & 0x7F, (float)(a ? a : 1) * (1.0f / 127.0f)); break;
        case C_VP: vp_cmd(t, a, b); break;
        default: break;
        }
        BARRIER();
        q_r++;
    }
}
EN_COLD void en_init(void)
{
    int i, t;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    for (i = 0; i < EN_NPAD; i++) {
        static const float PAN[EN_NPAD] = {-0.3f, -0.09f, 0.12f, 0.33f};
        const float p = (PAN[i] + 1.0f) * (FM_PI / 4.0f);
        pan_l[i] = fm_cosf(p);
        pan_r[i] = fm_sinf(p);
    }
    q_r = q_w;
    en_tempo = 122, en_seq_playing = en_seq_rec = 0, en_metro_on = 0, en_metro_lvl = 60;
    for (t = 0; t < NT; t++)
        trk_reset(t);
    sq_t = 0;
    quiet();
    pl_decay = pl_decay_t;
}
void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    {
        PROF_B(p0);
        drain();
        PROF_E(0, p0);
    }
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        const uint32_t w = (seq_events(), seq_wait());   /* (a step or a beat due: now, on its sample) */
        if (w < k)
            k = w ? w : 1u;
        /* once a chunk: the plate's decay eased to the page's (Freeze: held) */
        pl_decay += ((de_rfreeze ? 0.9995f : pl_decay_t) - pl_decay) * 0.002f;
        pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
        render_chunk(out, k, gain);
        seq_advance(out, k, gain);
        out += 2u * k;
        n -= k;
    }
}
