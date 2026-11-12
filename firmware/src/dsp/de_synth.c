/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine: the chord's synth. Included by amb.c after render_stabs (it uses the engine's buffers, its filter and its
 * sine table); played instead of DUB-1's chord voices while en_gen_on is 0.
 *
 *   four voices (the chord's notes, stab[i].note), each two oscillators detuned apart: sine, triangle, saw or square
 *   -> a wavefolder (a sine fold: Fold drives it harder)
 *   -> one resonant low-pass for the chord, left and right (the voices spread across)
 *   -> the amp, its envelope
 *   -> dry, and sends to the tape echo and the plate
 *
 * Four ADSR envelopes: the amp's, the filter's (on the cutoff by Env amount), Env 1 and Env 2 (each to a target); two
 * LFOs (each to a target). The envelopes, the LFOs and the modulation are worked out every 32 samples. */

const char *const EN_WAVE_NAME[EN_NWAVE] = {"Sine", "Tri", "Saw", "Square"};
const char *const EN_LS_NAME[EN_NLSHAPE] = {"Sine", "Tri", "Saw", "Square", "Random"};
const char *const EN_MD_NAME[EN_NMD] = {"Off", "Pitch", "Fold", "Detune", "Cutoff", "Reso", "Level", "Amp A", "Amp D",
                                        "Amp R", "Delay mix"};
const char *const EN_DTIME_NAME[EN_NDTIME] = {"1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/2.", "1 bar"};
static const float DTIME_BEATS[EN_NDTIME] = {0.25f, 0.3333333f, 0.5f, 0.75f, 0.6666667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
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
};
volatile int16_t en_syn[SY_N];
volatile float en_syn_amp;

enum { ENV_AMP, ENV_FILT, ENV_1, ENV_2, NENV };
enum { ST_IDLE, ST_ATT, ST_DEC, ST_REL };
static struct {
    float lv;                                    /* 0..1 */
    uint8_t st;
} sy_env[NENV];
static float sy_ph[EN_NPAD][2];                  /* the oscillators' phases, in turns */
static float sy_s[4], sy_c[4];                   /* the filter's state (left, right), its coefficients */
static float sy_lfo_ph[2], sy_lfo_v[2], sy_rand[2], sy_amp_s;
static uint8_t sy_ready;

void en_syn_set(int p, int v) { post(C_SYN, p, v); }
/* the delay and the reverb: DUB-1's tape echo and plate, set from the Delay and Reverb pages (while en_gen_on is 0) */
static float de_dmix, de_rmix, de_dry = 1.0f, de_rlow, de_rwidth = 1.0f, de_pdamp = 0.64f, de_wph1, de_wph2, de_rhp, de_rhc = 1.0f,
             de_rlp[2];
static int de_pre = PRE_N_DUB;
static void syn_fx(void)
{
    const float dm = (float)en_syn[SY_DMIX] * 0.01f, rm = (float)en_syn[SY_RMIX] * 0.01f;
    /* Mix: up to the middle the send rises to full, past it the dry falls away (two effects: the dry the less of both) */
    de_dmix = fm_minf(2.0f * dm, 1.0f);
    de_rmix = fm_minf(2.0f * rm, 1.0f);
    de_dry = fm_minf(2.0f * (1.0f - dm), 1.0f) * fm_minf(2.0f * (1.0f - rm), 1.0f);
    de_wow = (float)en_syn[SY_DWOW] * 0.01f;
    de_drive = (float)en_syn[SY_DDRIVE] * 0.01f;
    apply(P_ETONE, en_syn[SY_DTONE]);            /* (what enters the tape) */
    echo_time();
    pl_decay_t = 0.45f + 0.0051f * (float)en_syn[SY_RDECAY];   /* (to 0.96) */
    de_pdamp = 0.95f - 0.8f * (float)en_syn[SY_RDAMP] * 0.01f;
    de_pre = 1 + (int)((float)(PRE_N - 2) * (float)en_syn[SY_RPRE] * 0.01f);   /* (1 .. 100 ms) */
    de_rlow = (float)en_syn[SY_RLOW] * 0.01f * 0.11f;   /* (a one-pole high-pass: to ~800 Hz) */
    de_rwidth = (float)en_syn[SY_RWIDTH] * 0.02f;   /* (0 mono, 1 as the plate is, 2 wider) */
    de_rv_sz = 0.35f + 0.0065f * (float)en_syn[SY_RSIZE];   /* (the plate's lines read shorter: a smaller room) */
    de_rv_diff = (float)en_syn[SY_RDIFF] * 0.0125f;   /* (0 .. 1.25 of the plate's own diffusion; 75: as it was) */
    de_rv_md = (float)en_syn[SY_RMODD] * 0.01f;
    de_rv_mr = 0.00002f * fm_exp2f((float)en_syn[SY_RMODR] * 0.05f);   /* (~0.07 .. 2.3 Hz at half the rate) */
    de_rhc = 1.0f - fm_expf(-2.0f * FM_PI * (1000.0f * fm_exp2f((float)en_syn[SY_RHICUT] * 0.0432f)) / EN_SR);   /* (1 .. 20 kHz) */
    de_rfreeze = (uint8_t)(en_syn[SY_RFREEZE] != 0);
}
static inline int de_pre_n(void) { return de_pre; }
static inline float pl_damp_k(void) { return en_gen_on ? 0.64f : de_pdamp; }
static float de_dtime_beats(void) { return DTIME_BEATS[(unsigned)en_syn[SY_DTIME] < EN_NDTIME ? en_syn[SY_DTIME] : 3]; }
static inline float de_wow_step(void)             /* a tape sample on: the sway (~0.6 Hz) and the flutter (~6.5 Hz), -1..1 */
{
    de_wph1 += 0.6f / TAPE_SR;
    de_wph2 += 6.5f / TAPE_SR;
    if (de_wph1 >= 1.0f)
        de_wph1 -= 1.0f;
    if (de_wph2 >= 1.0f)
        de_wph2 -= 1.0f;
    return 0.8f * sine(de_wph1) + 0.2f * sine(de_wph2);
}
static inline float de_drive_in(float x)          /* Drive: the tape pushed, saturating, its level kept */
{
    const float g = 1.0f + 5.0f * de_drive;
    return sat(x * g) * (1.0f + 0.5f * de_drive) / g;
}
static void syn_defaults(void)
{
    int i;
    for (i = 0; i < SY_N; i++)
        en_syn[i] = SYN_INFO[i].def;
    sy_ready = 1;
    syn_fx();
}
static void syn_cmd(int p, int v)
{
    if ((unsigned)p >= SY_N)
        return;
    if (!sy_ready)
        syn_defaults();
    v = (int16_t)v;                              /* (posted as 16 bits: a negative depth back) */
    en_syn[p] = (int16_t)(v < SYN_INFO[p].lo ? SYN_INFO[p].lo : v > SYN_INFO[p].hi ? SYN_INFO[p].hi : v);
    syn_fx();
}

static void syn_trig(void)                       /* a chord struck: every envelope from where it is, to its attack */
{
    int i;
    if (!sy_ready)
        syn_defaults();
    for (i = 0; i < NENV; i++)
        sy_env[i].st = ST_ATT;
    sy_rand[0] = sy_rand[1] = 0.0f;
}

static float sy_env_time(int v, float mul)          /* 0..100 -> 1 ms .. 10 s (exponential), times a modulation */
{
    return 0.001f * fm_exp2f((float)v * 0.1329f) * mul;   /* (2^13.29 = 10000) */
}
static float sy_env_step(int e, int a, int d, int s, int r, float am, float dm, float rm, int gate)   /* 32 samples on */
{
    float *lv = &sy_env[e].lv;
    const float sus = (float)s * 0.01f;
    if (!gate && sy_env[e].st != ST_IDLE)
        sy_env[e].st = ST_REL;
    switch (sy_env[e].st) {
    case ST_ATT: {                               /* a straight line up */
        *lv += 32.0f / (sy_env_time(a, am) * EN_SR);
        if (*lv >= 1.0f)
            *lv = 1.0f, sy_env[e].st = ST_DEC;
        break;
    }
    case ST_DEC:                                 /* an exponential fall to the sustain (the time to ~-60 dB of it) */
        *lv = sus + (*lv - sus) * fm_expf(-32.0f * 6.9f / (sy_env_time(d, dm) * EN_SR));
        break;
    case ST_REL:
        *lv *= fm_expf(-32.0f * 6.9f / (sy_env_time(r, rm) * EN_SR));
        if (*lv < 1e-4f)
            *lv = 0.0f, sy_env[e].st = ST_IDLE;
        break;
    default:
        *lv = 0.0f;
        break;
    }
    return *lv;
}

static float sy_lfo_step(int k)                     /* LFO k, 32 samples on: -1 .. +1 */
{
    const int rate = en_syn[k ? SY_L2RATE : SY_L1RATE], shape = en_syn[k ? SY_L2SHAPE : SY_L1SHAPE],
              sync = en_syn[k ? SY_L2SYNC : SY_L1SYNC];
    float hz, ph, y;
    if (sync > 0 && sync < EN_NSYNC)             /* in time: a cycle as many beats */
        hz = 1.0f / (SYNC_BEATS[sync] * quarter() * (1.0f / EN_SR));
    else
        hz = 0.02f * fm_exp2f((float)rate * 0.0997f);   /* 0.02 Hz .. 20 Hz */
    ph = sy_lfo_ph[k] + hz * 32.0f * (1.0f / EN_SR);
    if (ph >= 1.0f) {
        ph -= (float)(int)ph;
        sy_rand[k] = 2.0f * frand() - 1.0f;      /* (the random: a new value each cycle) */
    }
    sy_lfo_ph[k] = ph;
    switch (shape) {
    case EN_LS_TRI: y = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case EN_LS_SAW: y = 2.0f * ph - 1.0f; break;
    case EN_LS_SQUARE: y = ph < 0.5f ? 1.0f : -1.0f; break;
    case EN_LS_RANDOM: y = sy_rand[k]; break;
    default: y = sine(ph); break;
    }
    sy_lfo_v[k] = y;
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

static void render_synth(uint32_t n)
{
    uint32_t j;
    int i;
    float md[EN_NMD], f[EN_NPAD], dt[EN_NPAD][2], fold, fdrive, amp0, amp1, gain, echo, plate;
    float s0 = sy_s[0], s1 = sy_s[1], s2 = sy_s[2], s3 = sy_s[3];
    const int gate = key_held || sq_gate > 0.0f;
    if (!sy_ready)
        syn_defaults();
    for (j = 0; j < n; j += 32u) {
        const uint32_t m = n - j < 32u ? n - j : 32u;
        float e1, e2, l1, l2, fe, cut, k, det, lvl;
        uint32_t t;
        /* the modulation: Env 1, Env 2, the LFOs, each to its target */
        for (i = 0; i < EN_NMD; i++)
            md[i] = 0.0f;
        e1 = sy_env_step(ENV_1, en_syn[SY_1A], en_syn[SY_1D], en_syn[SY_1S], en_syn[SY_1R], 1.0f, 1.0f, 1.0f, gate);
        e2 = sy_env_step(ENV_2, en_syn[SY_2A], en_syn[SY_2D], en_syn[SY_2S], en_syn[SY_2R], 1.0f, 1.0f, 1.0f, gate);
        l1 = sy_lfo_step(0);
        l2 = sy_lfo_step(1);
        md[en_syn[SY_1DST] % EN_NMD] += e1 * (float)en_syn[SY_1AMT] * 0.01f;
        md[en_syn[SY_2DST] % EN_NMD] += e2 * (float)en_syn[SY_2AMT] * 0.01f;
        md[en_syn[SY_L1DST] % EN_NMD] += l1 * (float)en_syn[SY_L1AMT] * 0.01f;
        md[en_syn[SY_L2DST] % EN_NMD] += l2 * (float)en_syn[SY_L2AMT] * 0.01f;
        /* the amp's envelope (its times moved by their targets: x16 .. /16) and the filter's */
        amp0 = sy_amp_s;
        amp1 = sy_env_step(ENV_AMP, en_syn[SY_AA], en_syn[SY_AD], en_syn[SY_AS], en_syn[SY_AR], fm_exp2f(4.0f * md[EN_MD_AATT]),
                        fm_exp2f(4.0f * md[EN_MD_ADEC]), fm_exp2f(4.0f * md[EN_MD_AREL]), gate);
        fe = sy_env_step(ENV_FILT, en_syn[SY_FA], en_syn[SY_FD], en_syn[SY_FS], en_syn[SY_FR], 1.0f, 1.0f, 1.0f, gate);
        /* the oscillators: each voice's pitch (Pitch: +-12 semitones), its two apart by Detune (to +-25 cents) */
        det = fm_clampf((float)en_syn[SY_DETUNE] * 0.01f + md[EN_MD_DETUNE], 0.0f, 1.0f) * 0.0145f;
        for (i = 0; i < EN_NPAD; i++) {
            f[i] = mtof((float)stab[i].note + 12.0f * md[EN_MD_PITCH]);
            dt[i][0] = f[i] * (1.0f - det) * (1.0f / EN_SR);
            dt[i][1] = f[i] * (1.0f + det) * (1.0f / EN_SR);
        }
        fold = fm_clampf((float)en_syn[SY_FOLD] * 0.01f + md[EN_MD_FOLD], 0.0f, 1.0f);
        fdrive = 0.0625f + 0.8125f * fold;      /* (turns of the fold's sine a unit of the two oscillators: at 0 an
                                                  * eighth of a turn at their peak, nearly straight; at 100 past two folds) */
        /* the filter: Cutoff 40 Hz .. 16 kHz, the envelope +-6 octaves, Key track from the chord's middle (C4), the
         * target's +-5 octaves */
        cut = 40.0f * fm_exp2f((float)en_syn[SY_CUT] * 0.0864f + 6.0f * fe * (float)en_syn[SY_FENV] * 0.01f +
                               (float)en_syn[SY_KTRK] * 0.01f * ((float)stab[1].note - 60.0f) * (1.0f / 12.0f) +
                               5.0f * md[EN_MD_CUTOFF]);
        k = 2.0f - 1.9f * fm_clampf((float)en_syn[SY_RESO] * 0.01f + md[EN_MD_RESO], 0.0f, 1.0f);
        svf_coefs(sy_c, fm_clampf(cut, 30.0f, 16000.0f), k);
        lvl = fm_clampf((float)en_syn[SY_LEVEL] * 0.01f * (1.0f + md[EN_MD_LEVEL]), 0.0f, 1.5f);
        gain = lvl * 0.28f;
        echo = fm_clampf(de_dmix + md[EN_MD_ECHO], 0.0f, 1.0f) * 0.9f;
        plate = de_rmix * 0.6f;
        if (amp0 < 1e-5f && amp1 < 1e-5f) {     /* (silent: the oscillators run on, nothing heard) */
            for (i = 0; i < EN_NPAD; i++) {
                sy_ph[i][0] += dt[i][0] * (float)m;
                sy_ph[i][1] += dt[i][1] * (float)m;
                sy_ph[i][0] -= (float)(int)sy_ph[i][0];
                sy_ph[i][1] -= (float)(int)sy_ph[i][1];
            }
            sy_amp_s = amp1;
            continue;
        }
        for (t = 0; t < m; t++) {
            const uint32_t jj = j + t;
            const float am = amp0 + (amp1 - amp0) * (float)t * (1.0f / 32.0f);
            float l = 0.0f, r = 0.0f, v1, v2, v3, yl, yr;
            for (i = 0; i < EN_NPAD; i++) {
                float x = sy_osc(en_syn[SY_WAVE], sy_ph[i][0], dt[i][0]) + sy_osc(en_syn[SY_WAVE], sy_ph[i][1], dt[i][1]);
                x = sine(x * fdrive + 4.0f);     /* the fold: driven past the edge, it folds back (a sine of it) */
                sy_ph[i][0] += dt[i][0];
                if (sy_ph[i][0] >= 1.0f)
                    sy_ph[i][0] -= 1.0f;
                sy_ph[i][1] += dt[i][1];
                if (sy_ph[i][1] >= 1.0f)
                    sy_ph[i][1] -= 1.0f;
                l += x * stab_gl[i];
                r += x * stab_gr[i];
            }
            v3 = l - s1;
            v1 = sy_c[0] * s0 + sy_c[1] * v3;
            v2 = s1 + sy_c[1] * s0 + sy_c[2] * v3;
            s0 = 2.0f * v1 - s0;
            s1 = 2.0f * v2 - s1;
            yl = v2;
            v3 = r - s3;
            v1 = sy_c[0] * s2 + sy_c[1] * v3;
            v2 = s3 + sy_c[1] * s2 + sy_c[2] * v3;
            s2 = 2.0f * v1 - s2;
            s3 = 2.0f * v2 - s3;
            yr = v2;
            yl = sat(yl * am * gain * 2.0f) * 0.5f;
            yr = sat(yr * am * gain * 2.0f) * 0.5f;
            mixl[jj] += yl * de_dry;
            mixr[jj] += yr * de_dry;
            esend[jj] += (yl + yr) * echo;
            rsend[jj] += (yl + yr) * plate;
        }
        sy_amp_s = amp1;
    }
    sy_s[0] = fm_flush(s0);
    sy_s[1] = fm_flush(s1);
    sy_s[2] = fm_flush(s2);
    sy_s[3] = fm_flush(s3);
    en_syn_amp = sy_amp_s;
    en_pad_level = fm_minf(sy_amp_s * 1.2f, 1.0f);
}
