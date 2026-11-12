/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine's panel: the key, the metronome and the pattern (docs/CONTROLS.md). Included by the unity build and the
 * host simulator after ui.c, whose helpers it uses (knob_set, set_mode, mix, mark_dirty, midi_in, pool_abs). While
 * DUB-1's generative layers are on (en_gen_on), DUB-1's own panel runs instead (ui_frame).
 *
 *   PLAY        start / stop
 *   REC         playing: live recording on / off; stopped: step edit on / off (in step edit: off)
 *   SEL         the metronome on / off
 *   HOME        the Play page: Tempo, Root, Mode, Voicing
 *   SEQ         the Pattern page: Length, Step, Click level; held 1.5 s: clear (held again within 5 s: back)
 *   GLO held    save
 *   OCT- / +    the keys' octave; in step edit, the page of 16 steps
 *   keys        lit (in the scale): their chords; in step edit the white keys are the steps: a tap puts the last
 *               chord on a step or takes it off, a step held and a knob turned locks it (Chord, Voicing, Octave,
 *               Length) */

enum { DE_PLAY, DE_PATTERN, DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2, DE_L1, DE_L2, DE_DELAY, DE_REVERB, DE_NPAGE };
/* the sound pages: each its four knobs (a synth parameter, SY_*; DE_P + a DUB-1 parameter, P_*), and ALGORITHM's and
 * PRESETS' (-1 none) */
#define DE_P 1000
static const int16_t DE_KNOBS[DE_NPAGE][6] = {
    {0}, {0},
    {SY_WAVE, SY_FOLD, SY_DETUNE, SY_LEVEL, -1, -1},
    {SY_CUT, SY_RESO, SY_FENV, SY_KTRK, -1, -1},
    {SY_AA, SY_AD, SY_AS, SY_AR, -1, -1},
    {SY_FA, SY_FD, SY_FS, SY_FR, -1, -1},
    {SY_1A, SY_1D, SY_1S, SY_1R, SY_1DST, SY_1AMT},
    {SY_2A, SY_2D, SY_2S, SY_2R, SY_2DST, SY_2AMT},
    {SY_L1RATE, SY_L1SHAPE, SY_L1DST, SY_L1AMT, SY_L1SYNC, -1},
    {SY_L2RATE, SY_L2SHAPE, SY_L2DST, SY_L2AMT, SY_L2SYNC, -1},
    {SY_DTIME, SY_DFB, SY_DMIX, SY_DTONE, SY_DWOW, SY_DDRIVE},
    {SY_RDECAY, SY_RMIX, SY_RPRE, SY_RDAMP, SY_RLOW, SY_RWIDTH},
};
static const char *const DE_PAGE_NAME[DE_NPAGE] = {"PLAY", "PATTERN", "OSC", "FILTER", "AMP ENV", "FILTER ENV", "ENV 1",
                                                   "ENV 2", "LFO 1", "LFO 2", "DELAY", "REVERB"};
static const uint8_t DE_ENV_CYCLE[6] = {DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2};
static int de_sound_page(int p) { return p >= DE_OSC && p < DE_NPAGE; }
#define DE_HOLD_MS 1500u                         /* SEQ held: clear */
#define DE_UNDO_MS 5000u                         /* (then SEQ held again: back) */
#define DE_SAVE_MS 1000u                         /* GLO held: save */
#define DE_MSG_MS 1600u
static const uint8_t DE_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const char *const DE_VO_NAME[4] = {"Root", "1st", "2nd", "Key"};
static const char *const DE_Q_NAME[EN_Q_OTHER + 1] = {"", "m", "dim", "aug", "sus"};

static struct {
    uint8_t page, edit, oct, spage, voicing;     /* page; step edit; the keys' octave (0..2: -1..+1); step page; voicing */
    uint32_t btn, keys;
    uint32_t down_t[NB];
    uint8_t held_done[NB];                       /* (a hold's action done: its release does nothing) */
    uint8_t note[NKEYS];                         /* (each key's note as pressed: let go the same) */
    int8_t step;                                 /* the step held in step edit, -1 none */
    uint8_t step_turned, step_was_on;            /* (a knob turned while it's held; it was on when pressed) */
    en_step_t last;                              /* the last chord set: a new step's */
    uint32_t cleared_t;                          /* when the pattern was cleared (0: not lately) */
    uint32_t msg_t;
    char msg[24];
    uint32_t act_t;
} de;

static int de_count(uint32_t p)
{
    int n = 0;
    for (; p; p &= p - 1u)
        n++;
    return n;
}
static void de_num(char *b, int v)
{
    char t[8];
    int n = 0, i = 0;
    if (v < 0)
        *b++ = '-', v = -v;
    do
        t[n++] = (char)('0' + v % 10), v /= 10;
    while (v && n < 7);
    while (n)
        b[i++] = t[--n];
    b[i] = 0;
}
static void de_cat(char *b, const char *s)
{
    while (*b)
        b++;
    while (*s)
        *b++ = *s++;
    *b = 0;
}

static void de_say(const char *s)
{
    uint32_t i;
    for (i = 0; i < sizeof de.msg - 1u && s[i]; i++)
        de.msg[i] = s[i];
    de.msg[i] = 0;
    de.msg_t = plat_ms();
}
static void de_chord_name(const en_step_t *st, char *b)   /* "Cm", "Ddim", "Eb" */
{
    int s[12], m = 0, i, pc, t1, t2, q;
    for (i = 0; i < 12; i++)
        if (proj.pool >> i & 1u)
            s[m++] = i;
    if (m < 3) {
        b[0] = '?', b[1] = 0;
        return;
    }
    pc = (proj.par[P_ROOT] + s[st->deg % m]) % 12;
    t1 = s[(st->deg + 2) % m] + 12 * ((st->deg + 2) / m) - s[st->deg % m];
    t2 = s[(st->deg + 4) % m] + 12 * ((st->deg + 4) / m) - s[st->deg % m];
    q = t1 == 4 && t2 == 7 ? EN_Q_MAJ : t1 == 3 && t2 == 7 ? EN_Q_MIN : t1 == 3 && t2 == 6 ? EN_Q_DIM : t1 == 4 && t2 == 8 ? EN_Q_AUG : EN_Q_OTHER;
    for (i = 0; EN_NOTE_NAME[pc][i]; i++)
        *b++ = EN_NOTE_NAME[pc][i];
    for (i = 0; DE_Q_NAME[q][i]; i++)
        *b++ = DE_Q_NAME[q][i];
    *b = 0;
}
static int de_nsteps(void) { return en_seq_len; }
static int de_spages(void) { return (de_nsteps() + 15) / 16; }

static void de_init(void)
{
    int i;
    memset(&de, 0, sizeof de);
    de.page = DE_PLAY;
    de.oct = 1;
    de.step = -1;
    de.voicing = EN_VO_FIRST;
    de.last.on = 1, de.last.deg = 0, de.last.oct = 2, de.last.voi = EN_VO_KEY, de.last.len = 1;
    for (i = 0; i < NB; i++)
        de.held_done[i] = 0;
    en_voicing_set(de.voicing);
    en_metro(0, 60);
}

/* ---------------------------------------------------------------- input --- */
static void de_button(int b)                    /* a press */
{
    switch (b) {
    case B_PLAY:
        en_seq_play(!en_seq_playing);
        if (en_seq_playing && en_seq_rec)        /* (stopping ends a take) */
            en_seq_record(0);
        break;
    case B_REC:
        if (de.edit) {
            de.edit = 0;
            de.step = -1;
        } else if (en_seq_playing) {
            en_seq_record(!en_seq_rec);
        } else {
            de.edit = 1;
            de.spage = 0;
            en_seq_record(0);
        }
        break;
    case B_SEL:
        en_metro(!en_metro_on, en_metro_lvl);
        de_say(en_metro_on ? "CLICK OFF" : "CLICK ON");   /* (the engine's flag: the state before this press) */
        break;
    case B_HOME:
        de.page = DE_PLAY;
        break;
    case B_ENV: {                                /* Osc, Filter, Amp env, Filter env, Env 1, Env 2, in turn */
        int i, at = -1;
        for (i = 0; i < 6; i++)
            if (DE_ENV_CYCLE[i] == de.page)
                at = i;
        de.page = DE_ENV_CYCLE[at < 0 ? 0 : (at + 1) % 6];
        break;
    }
    case B_LFO:
        de.page = de.page == DE_L1 ? DE_L2 : DE_L1;
        break;
    case B_FX:                                   /* Delay, Reverb, in turn */
        de.page = de.page == DE_DELAY ? DE_REVERB : DE_DELAY;
        break;
    case B_SEQ:
        de.page = DE_PATTERN;
        break;
    case B_OCTDN:
    case B_OCTUP: {
        const int up = b == B_OCTUP;
        if (de.edit) {
            const int n = de_spages();
            de.spage = (uint8_t)((de.spage + (up ? 1 : n - 1)) % n);
        } else if (up ? de.oct < 2 : de.oct > 0) {
            de.oct = (uint8_t)(de.oct + (up ? 1 : -1));
        }
        break;
    }
    default:
        break;
    }
}
static void de_hold(int b, uint32_t ms)          /* held this long: SEQ clears (or brings back), GLO saves */
{
    if (de.held_done[b])
        return;
    if (b == B_SEQ && ms >= DE_HOLD_MS) {
        const uint32_t now = plat_ms();
        de.held_done[b] = 1;
        if (de.cleared_t && now - de.cleared_t < DE_UNDO_MS) {
            en_seq_restore();
            de.cleared_t = 0;
            de_say("PATTERN BACK");
        } else {
            en_seq_clear();
            de.cleared_t = now;
            de_say("CLEARED");
        }
    } else if (b == B_GLO && ms >= DE_SAVE_MS) {
        de.held_done[b] = 1;
        de_say(project_save() == 0 ? "SAVED" : "SAVE FAILED");
    }
}

static int de_white(int key)                     /* a key's step on the step page (white keys), -1 a black key */
{
    int i;
    for (i = 0; i < 16; i++)
        if (DE_WHITE[i] == key)
            return de.spage * 16 + i;
    return -1;
}
static void de_step_get(int i, en_step_t *t)
{
    t->on = en_seq[i].on, t->deg = en_seq[i].deg, t->oct = en_seq[i].oct, t->voi = en_seq[i].voi, t->len = en_seq[i].len;
}
static void de_key(int key, int down)
{
    if (de.edit) {                               /* step edit: the white keys are the steps */
        const int st = de_white(key);
        en_step_t t;
        if (st < 0 || st >= de_nsteps())
            return;
        if (down) {
            de_step_get(st, &t);
            de.step = (int8_t)st;
            de.step_turned = 0;
            de.step_was_on = t.on;
            if (!t.on) {                         /* a new step: the last chord set */
                t = de.last;
                t.on = 1;
                en_seq_step(st, &t);
            }
            de.note[key] = (uint8_t)en_seq_note(&t);   /* (heard while held) */
            if (!en_seq_playing)
                en_key_chord(de.note[key], 1);
        } else if (de.step == st) {
            if (de.step_was_on && !de.step_turned) {   /* a tap on a step: off */
                de_step_get(st, &t);
                t.on = 0;
                en_seq_step(st, &t);
            }
            if (!en_seq_playing)
                en_key_chord(de.note[key], 0);
            de.step = -1;
        }
        return;
    }
    if (down) {                                  /* playing: a key in the scale, its chord */
        const int note = 53 + key + 12 * ((int)de.oct - 1);
        if (!(pool_abs() >> ((5u + (uint32_t)key) % 12u) & 1u))
            return;
        de.note[key] = (uint8_t)note;
        en_key_chord(note, 1);
        if (en_note_step(note, &de.last))        /* (the last chord set: the next new step's) */
            de.last.voi = EN_VO_KEY, de.last.len = 1;
    } else if (de.note[key]) {
        en_key_chord(de.note[key], 0);
        de.note[key] = 0;
    }
}

static int de_param_get(int id)                  /* a page's parameter's value now */
{
    return id >= DE_P ? proj.par[id - DE_P] : id >= 0 ? en_syn[id] : 0;
}
static void de_param_turn(int id, int d)         /* turned d detents: a click a step (a wide range: two) */
{
    if (id < 0)
        return;
    if (id >= DE_P) {
        knob_set(id - DE_P, proj.par[id - DE_P] + d);
        mark_dirty();
        return;
    }
    {
        const int lo = SYN_INFO[id].lo, hi = SYN_INFO[id].hi, st = hi - lo > 40 ? 2 : 1;
        int v = en_syn[id] + d * st;
        v = v < lo ? lo : v > hi ? hi : v;
        en_syn_set(id, v);
        en_syn[id] = (int16_t)v;                 /* (shown at once: the engine sets the same) */
    }
}
static const char *de_param_name(int id)
{
    return id >= DE_P ? (id - DE_P == P_ETIME ? "Time" : id - DE_P == P_FEEDBACK ? "Feedback" : "") : id >= 0 ? SYN_INFO[id].name : "";
}
static void de_param_text(int id, char *b)        /* its value as shown */
{
    const int v = de_param_get(id);
    b[0] = 0;
    if (id < 0)
        return;
    if (id >= DE_P) {
        en_param_text(id - DE_P, v, b);
        return;
    }
    switch (id) {
    case SY_WAVE: de_cat(b, EN_WAVE_NAME[v % EN_NWAVE]); return;
    case SY_L1SHAPE: case SY_L2SHAPE: de_cat(b, EN_LS_NAME[v % EN_NLSHAPE]); return;
    case SY_1DST: case SY_2DST: case SY_L1DST: case SY_L2DST: de_cat(b, EN_MD_NAME[v % EN_NMD]); return;
    case SY_L1SYNC: case SY_L2SYNC: de_cat(b, EN_SYNC_NAME[v % EN_NSYNC]); return;
    case SY_DTIME: de_cat(b, EN_DTIME_NAME[v % EN_NDTIME]); return;
    case SY_DFB: case SY_DMIX: case SY_RMIX: de_num(b, v); de_cat(b, "%"); return;
    case SY_RPRE: de_num(b, 1 + v); de_cat(b, "ms"); return;
    default: break;
    }
    if (SYN_INFO[id].lo < 0 && v > 0)
        de_cat(b, "+");
    de_num(b + (b[0] ? 1 : 0), v);
}

static void de_knob(int k, int d)               /* KNOB 1..4 (k 0..3) turned d detents */
{
    if (de.edit && de.step >= 0) {               /* a step held: its locks */
        const int m = de_count(proj.pool) > 0 ? de_count(proj.pool) : 1;
        en_step_t t;
        de_step_get(de.step, &t);
        if (k == 0)
            t.deg = (uint8_t)(((int)t.deg + d % m + m) % m);
        else if (k == 1)
            t.voi = (uint8_t)(((int)t.voi + d % 4 + 4) % 4);
        else if (k == 2)
            t.oct = (uint8_t)((int)t.oct + d < 0 ? 0 : (int)t.oct + d > 3 ? 3 : (int)t.oct + d);
        else
            t.len = (uint8_t)((int)t.len + d < 1 ? 1 : (int)t.len + d > de_nsteps() ? de_nsteps() : (int)t.len + d);
        t.on = 1;
        en_seq_step(de.step, &t);
        de.last = t;
        de.step_turned = 1;
        if (k <= 2 && !en_seq_playing) {         /* (the new chord heard) */
            const int key = DE_WHITE[de.step % 16];
            en_key_chord(de.note[key], 0);
            de.note[key] = (uint8_t)en_seq_note(&t);
            if (t.voi != EN_VO_KEY)
                en_voicing_set(t.voi);
            en_key_chord(de.note[key], 1);
            en_voicing_set(de.voicing);
        }
        return;
    }
    if (de_sound_page(de.page)) {
        de_param_turn(DE_KNOBS[de.page][k], d);
        return;
    }
    if (de.page == DE_PLAY) {
        if (k == 0)
            knob_set(P_TEMPO, proj.par[P_TEMPO] + d), mark_dirty();
        else if (k == 1)
            knob_set(P_ROOT, ((proj.par[P_ROOT] + d) % 12 + 12) % 12), mark_dirty();
        else if (k == 2) {
            int m = proj.par[P_MODE];
            m = m > MODE_AEOLIAN ? MODE_AEOLIAN : m;
            set_mode(((m + d) % (MODE_AEOLIAN + 1) + MODE_AEOLIAN + 1) % (MODE_AEOLIAN + 1));
        } else {
            de.voicing = (uint8_t)(((int)de.voicing + d % 3 + 3) % 3);
            en_voicing_set(de.voicing);
        }
    } else {
        if (k == 0) {
            int n = de_nsteps() + d;
            en_seq_length(n < 1 ? 1 : n > EN_SEQ_MAX ? EN_SEQ_MAX : n);
        } else if (k == 1) {
            int v = (int)en_seq_div + d;
            en_seq_division(v < 0 ? 0 : v >= EN_NDIV ? EN_NDIV - 1 : v);
        } else if (k == 2) {
            int v = (int)en_metro_lvl + 5 * d;
            en_metro(en_metro_on, v < 0 ? 0 : v > 100 ? 100 : v);
        }
    }
}

static void de_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    const uint32_t now = plat_ms();
    ch = btn ^ de.btn;
    de.btn = btn;
    for (i = 0; i < NB; i++) {
        const uint32_t m = 1u << i;
        if (ch & m) {
            if (btn & m) {
                de.down_t[i] = now;
                de.held_done[i] = 0;
                if (i != B_SEQ && i != B_GLO)    /* (SEQ and GLO act on release, or on a hold) */
                    de_button((int)i);
            } else if ((i == B_SEQ || i == B_GLO) && !de.held_done[i]) {
                de_button((int)i);
            }
        } else if (btn & m) {
            de_hold((int)i, now - de.down_t[i]);
        }
    }
    ch = keys ^ de.keys;
    de.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u)
            de_key((int)i, (int)(keys >> i & 1u));
    for (i = 0; i < 4; i++) {
        const int32_t d = plat_enc(EN_K1 + (int)i);
        if (d)
            de_knob((int)i, (int)d), de.act_t = now;
    }
    {   /* ALGORITHM and PRESETS: on Env 1, Env 2 the target and the depth; on the LFOs the sync */
        const int32_t da = plat_enc(EN_ALGO), dp = plat_enc(EN_PRESET);
        if (de_sound_page(de.page) && !(de.edit && de.step >= 0)) {
            if (da)
                de_param_turn(DE_KNOBS[de.page][4], (int)da), de.act_t = now;
            if (dp)
                de_param_turn(DE_KNOBS[de.page][5], (int)dp), de.act_t = now;
        }
    }
    plat_enc(EN_SELECT);                         /* (not used yet: its turns let go) */
    if (btn || keys || ch)
        de.act_t = now;
    midi_in();
}

/* ---------------------------------------------------------------- screen --- */
#define DE_HDR 30
#define DE_MAIN 140
#define DE_FOOT (240 - DE_HDR - DE_MAIN)

static void de_draw_header(void)
{
    char b[24];
    int32_t w;
    cv_begin(240, DE_HDR, K_BG);
    b[0] = 0;
    de_cat(b, EN_NOTE_NAME[proj.par[P_ROOT] % 12]);
    de_cat(b, " ");
    de_cat(b, EN_MODE_NAME[proj.par[P_MODE] % EN_NMODES]);
    cv_text(8, 7, &FONT_B, b, K_TEXT);
    de_num(b, proj.par[P_TEMPO]);
    de_cat(b, " BPM");
    w = text_w(&FONT_S, b);
    cv_text(232 - w, 7, &FONT_S, b, K_DIM);
    if (en_seq_rec || de.edit) {                 /* the tag: REC (red) or EDIT */
        const char *t = de.edit ? "EDIT" : "REC";
        const int32_t tw = text_w(&FONT_S, t) + 10, x = 232 - w - tw - 8;
        cv_rect(x, 6, tw, 18, de.edit ? K_ACC : K_FROST);
        cv_text(x + 5, 7, &FONT_S, t, K_BG);
    }
    cv_rect(0, DE_HDR - 1, 240, 1, K_LINE);
    cv_blit(0, 0);
}

static float de_sin(float t)                     /* sin(2 pi t), for the pictures */
{
    return fm_sin_turns(t - (float)(int)t + (t < 0.0f ? 1.0f : 0.0f));
}
static float de_wave(int w, float t, float fold)  /* one oscillator's picture, -1..1, t in turns, folded as the synth does */
{
    float x;
    t -= (float)(int)t;
    x = w == EN_WAVE_SINE ? de_sin(t) : w == EN_WAVE_TRI ? (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t)
        : w == EN_WAVE_SQUARE ? (t < 0.5f ? 1.0f : -1.0f) : 2.0f * t - 1.0f;
    return de_sin(2.0f * x * (0.0625f + 0.8125f * fold) + 4.0f);
}
static float de_lfo(int sh, float t)
{
    static const float RND[8] = {0.6f, -0.3f, 0.9f, -0.8f, 0.1f, -0.5f, 0.4f, -0.1f};
    return sh == EN_LS_TRI ? (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t) : sh == EN_LS_SAW ? 2.0f * t - 1.0f
           : sh == EN_LS_SQUARE ? (t < 0.5f ? 1.0f : -1.0f) : sh == EN_LS_RANDOM ? RND[(int)(t * 8.0f) & 7] : de_sin(t);
}
static void de_draw_sound(void)                  /* a sound page: its picture, and its targets */
{
    const int pg = de.page, x0 = 12, w = 216, y0 = 36, h = DE_KNOBS[pg][4] >= 0 ? 52 : 80, ym = y0 + h / 2;
    int i, py = 0;
    char b[32];
    cv_rect(x0, ym, w, 1, K_LINE);
    if (pg == DE_FILT) {                         /* the filter's response, 20 Hz .. 20 kHz across, its peak the Reso */
        const float fc = 40.0f * fm_exp2f((float)en_syn[SY_CUT] * 0.0864f),
                    k = 2.0f - 1.9f * (float)en_syn[SY_RESO] * 0.01f;
        cv_rect(x0, ym, w, 1, K_BG);
        cv_rect(x0, y0 + 24, w, 1, K_LINE);      /* (0 dB) */
        for (i = 0; i <= w; i++) {
            const float f = 20.0f * fm_exp2f(9.97f * (float)i / (float)w), r = f / fc, a = 1.0f - r * r, bq = k * r,
                        g = 1.0f / fm_sqrtf(a * a + bq * bq), db = 20.0f * fm_log2f(g > 1e-6f ? g : 1e-6f) * 0.30103f;
            int y = y0 + 24 - (int)(db * 1.2f);
            y = y < y0 ? y0 : y > y0 + h ? y0 + h : y;
            if (i)
                cv_line(x0 + i - 1, py, x0 + i, y, K_PAD);
            py = y;
        }
    } else if (pg == DE_DELAY) {                 /* the repeats: on the delay's time, each as Feedback leaves it, the
                                                  * first as loud as Mix sends; the dry bar as loud as Mix leaves it */
        static const float BEATS[EN_NDTIME] = {0.25f, 0.333f, 0.5f, 0.75f, 0.667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
        const float fb = fm_minf((float)en_syn[SY_DFB] * 0.01f, 1.0f), mx = (float)en_syn[SY_DMIX] * 0.01f,
                    sp = 26.0f * BEATS[en_syn[SY_DTIME] % EN_NDTIME];
        float lv = fm_minf(2.0f * mx, 1.0f), x = (float)x0 + sp;
        const int hd = (int)((float)(h - 4) * fm_minf(2.0f * (1.0f - mx), 1.0f));
        cv_rect(x0, ym, w, 1, K_BG);
        cv_rect(x0, y0 + h, w, 1, K_LINE);
        if (hd > 0)
            cv_rect(x0, y0 + h - hd, 5, hd, K_TEXT);
        for (i = 0; i < 40 && x < (float)(x0 + w - 4); i++, x += sp) {
            const int hh = (int)((float)(h - 4) * lv);
            if (hh > 0)
                cv_rect((int)x, y0 + h - hh, 4, hh, mix(K_PAD_T, K_PAD, 16 - (int)(16.0f * (float)en_syn[SY_DTONE] * 0.004f)));
            lv *= fb;
        }
    } else if (pg == DE_REVERB) {                /* the tail: the pre-delay's gap, then the decay, as Mix sends it */
        const float mx = fm_minf(2.0f * (float)en_syn[SY_RMIX] * 0.01f, 1.0f),
                    dk = 0.45f + 0.0051f * (float)en_syn[SY_RDECAY], pre = 2.0f + 0.25f * (float)en_syn[SY_RPRE];
        cv_rect(x0, ym, w, 1, K_BG);
        cv_rect(x0, y0 + h, w, 1, K_LINE);
        cv_rect(x0, y0 + 4, 5, h - 4, K_TEXT);
        for (i = (int)pre; i < w; i++) {
            const float t = (float)(i - (int)pre), a = mx * fm_expf(-t * (1.0f - dk) * 0.12f);
            const int hh = (int)((float)(h - 4) * a);
            if (hh > 0)
                cv_rect(x0 + 6 + i, y0 + h - hh, 1, hh, (i & 1) ? K_PAD_T : mix(K_PAD_T, K_PAD, 8));
        }
    } else if (pg == DE_OSC) {                   /* two cycles of the wave, as folded */
        const float fold = (float)en_syn[SY_FOLD] * 0.01f;
        for (i = 0; i <= w; i++) {
            const int y = ym - (int)(0.45f * (float)h * de_wave(en_syn[SY_WAVE], 2.0f * (float)i / (float)w, fold));
            if (i)
                cv_line(x0 + i - 1, py, x0 + i, y, K_PAD);
            py = y;
        }
    } else if (pg >= DE_AENV && pg <= DE_E2) {    /* the envelope: attack, decay to sustain, a hold, release */
        static const uint8_t A[4] = {SY_AA, SY_FA, SY_1A, SY_2A};
        const int e = A[pg - DE_AENV];
        const float ta = 0.2f + (float)en_syn[e], td = 0.2f + (float)en_syn[e + 1], sus = (float)en_syn[e + 2] * 0.01f,
                    tr = 0.2f + (float)en_syn[e + 3], tot = ta + td + 40.0f + tr, sc = (float)w / tot;
        const int xa = x0 + (int)(ta * sc), xd = xa + (int)(td * sc), xs = xd + (int)(40.0f * sc), yb = y0 + h, yt = y0 + 4,
                  ys = yb - (int)(sus * (float)(h - 4));
        cv_rect(x0, ym, w, 1, K_BG);
        cv_rect(x0, yb, w, 1, K_LINE);
        cv_line(x0, yb, xa, yt, K_PAD);
        for (i = xa; i < xd; i++) {             /* (the decay a curve) */
            const float f = (float)(i - xa) / (float)(xd - xa > 0 ? xd - xa : 1);
            const int y = ys - (int)((float)(ys - yt) * (1.0f - f) * (1.0f - f));
            cv_rect(i, y, 1, 2, K_PAD);
        }
        cv_line(xd, ys, xs, ys, K_PAD);
        for (i = xs; i < x0 + w; i++) {
            const float f = (float)(i - xs) / (float)(x0 + w - xs > 0 ? x0 + w - xs : 1);
            cv_rect(i, ys + (int)((float)(yb - ys) * (1.0f - (1.0f - f) * (1.0f - f))), 1, 2, K_PAD);
        }
    } else if (pg == DE_L1 || pg == DE_L2) {       /* two cycles of the LFO */
        const int sh = en_syn[pg == DE_L1 ? SY_L1SHAPE : SY_L2SHAPE];
        for (i = 0; i <= w; i++) {
            float t = 2.0f * (float)i / (float)w;
            int y;
            t -= (float)(int)t;
            y = ym - (int)(0.45f * (float)h * de_lfo(sh, t));
            if (i)
                cv_line(x0 + i - 1, py, x0 + i, y, K_BELL);
            py = y;
        }
    }
    /* the extra knobs: Env 1 and Env 2's target (ALGORITHM) and depth (PRESETS); the LFOs' sync (ALGORITHM) */
    for (i = 4; i < 6; i++) {
        const int id = DE_KNOBS[pg][i];
        int32_t x;
        if (id < 0)
            continue;
        b[0] = 0;
        de_cat(b, i == 4 ? "ALGORITHM  " : "PRESETS  ");
        x = cv_text(12, 98 + (i - 4) * 18, &FONT_XS, b, K_DIM);
        b[0] = 0;
        de_cat(b, de_param_name(id));
        de_cat(b, " ");
        de_param_text(id, b + __builtin_strlen(b));
        cv_text(x, 98 + (i - 4) * 18, &FONT_XS, b, K_TEXT);
    }
}

static void de_draw_main(void)
{
    char b[24];
    const uint32_t now = plat_ms();
    int i, rows, n = de_nsteps();
    cv_begin(240, DE_MAIN, K_BG);
    /* the chord: the one held or last struck; in step edit, the step held's */
    if (de.edit && de.step >= 0) {
        en_step_t t;
        de_step_get(de.step, &t);
        de_chord_name(&t, b);
    } else if (de.msg_t && now - de.msg_t < DE_MSG_MS) {
        for (i = 0; de.msg[i]; i++)
            b[i] = de.msg[i];
        b[i] = 0;
    } else {
        b[0] = 0;
        if (en_now_n) {
            de_cat(b, EN_NOTE_NAME[en_now_pc % 12]);
            de_cat(b, DE_Q_NAME[en_now_q <= EN_Q_OTHER ? en_now_q : EN_Q_OTHER]);
        } else {
            de_cat(b, "dubengine");
        }
    }
    if (de_sound_page(de.page) && !(de.edit && de.step >= 0)) {
        cv_text(12, 8, &FONT_B, b, de.msg_t && now - de.msg_t < DE_MSG_MS ? K_ACC : K_TEXT);
        de_draw_sound();
        cv_blit(0, DE_HDR);
        return;
    }
    {
        const felucca_font_t *f = text_w(&FONT_L, b) > 220 ? &FONT_B : &FONT_L;
        cv_text((240 - text_w(f, b)) / 2, 14, f, b, de.msg_t && now - de.msg_t < DE_MSG_MS ? K_ACC : K_TEXT);
    }
    /* the pattern: 16 a row, up to 4 rows; a step's colour its chord's degree, the playhead white */
    rows = (n + 15) / 16;
    for (i = 0; i < rows * 16; i++) {
        const int r = i / 16, c = i % 16, x = 8 + c * 14, y = 64 + r * 18;
        uint16_t col;
        if (i >= n)
            continue;
        if (en_seq[i].on)
            col = mix(K_PAD_T, K_PAD, 6 + 10 * (int)en_seq[i].deg / 6);
        else
            col = mix(K_BG, K_LINE, (c & 3) ? 8 : 14);
        if (en_seq_playing && i == en_seq_pos)
            col = K_TEXT;
        cv_rect(x, y, 12, 14, col);
        if (en_seq[i].on && en_seq[i].len > 1) {     /* (a held chord: a bar to its end, on its row) */
            int e = c + en_seq[i].len - 1;
            e = e > 15 ? 15 : e;
            cv_rect(x + 12, y + 6, (e - c) * 14, 2, K_PAD_T);
        }
        if (de.edit && r == de.spage)
            cv_rect(x, y + 15, 12, 2, K_ACC);        /* (the step page the keys edit) */
        if (de.edit && i == de.step)
            cv_rect(x, y - 3, 12, 2, K_ACC);
    }
    cv_blit(0, DE_HDR);
}

static void de_draw_foot(void)
{
    const char *lab[4];
    char val[4][12];
    int i;
    for (i = 0; i < 4; i++)
        val[i][0] = 0;
    if (de.edit && de.step >= 0) {
        en_step_t t;
        de_step_get(de.step, &t);
        lab[0] = "Chord", lab[1] = "Voicing", lab[2] = "Octave", lab[3] = "Length";
        de_chord_name(&t, val[0]);
        de_cat(val[1], DE_VO_NAME[t.voi & 3]);
        de_num(val[2], (int)t.oct - 2);
        de_num(val[3], t.len);
    } else if (de_sound_page(de.page)) {
        for (i = 0; i < 4; i++) {
            lab[i] = de_param_name(DE_KNOBS[de.page][i]);
            de_param_text(DE_KNOBS[de.page][i], val[i]);
        }
    } else if (de.page == DE_PLAY) {
        lab[0] = "Tempo", lab[1] = "Root", lab[2] = "Mode", lab[3] = "Voicing";
        de_num(val[0], proj.par[P_TEMPO]);
        de_cat(val[1], EN_NOTE_NAME[proj.par[P_ROOT] % 12]);
        de_cat(val[2], EN_MODE_NAME[proj.par[P_MODE] % EN_NMODES]);
        de_cat(val[3], DE_VO_NAME[de.voicing]);
    } else {
        lab[0] = "Length", lab[1] = "Step", lab[2] = "Click", lab[3] = "";
        de_num(val[0], de_nsteps());
        de_cat(val[1], EN_DIV_NAME[en_seq_div % EN_NDIV]);
        de_num(val[2], en_metro_lvl);
        de_cat(val[2], en_metro_on ? "" : " off");
    }
    cv_begin(240, DE_FOOT, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    cv_text(8, 4, &FONT_XS, de.edit && de.step >= 0 ? "STEP" : DE_PAGE_NAME[de.page], K_ACC);
    for (i = 0; i < 4; i++) {
        const int32_t x = i * 60, cx = x + 30;
        if (i)
            cv_rect(x, 24, 1, DE_FOOT - 30, K_LINE);
        cv_text(cx - text_w(&FONT_XS, lab[i]) / 2, 24, &FONT_XS, lab[i], K_DIM);
        cv_text(cx - text_w(&FONT_B, val[i]) / 2, 44, &FONT_B, val[i], K_TEXT);
    }
    cv_blit(0, DE_HDR + DE_MAIN);
}

static void de_leds(void)
{
    uint32_t b = 0, k = 0, i;
    const int blink = (plat_ms() / 150u) & 1u;
    if (en_seq_playing)
        b |= 1u << B_PLAY;
    if (en_seq_rec || (de.edit && blink))
        b |= 1u << B_REC;
    if (en_metro_on)
        b |= 1u << B_SEL;
    b |= 1u << (de.page == DE_PLAY ? B_HOME : de.page == DE_PATTERN ? B_SEQ : de.page >= DE_DELAY ? B_FX
               : de.page == DE_L1 || de.page == DE_L2 ? B_LFO : B_ENV);
    if (de.edit) {                               /* the white keys: the step page's steps, the playhead blinking */
        for (i = 0; i < 16; i++) {
            const int st = de.spage * 16 + (int)i;
            int on = st < de_nsteps() && en_seq[st].on;
            if (en_seq_playing && st == en_seq_pos && blink)
                on = !on;
            if (on)
                k |= 1u << DE_WHITE[i];
        }
    } else {                                     /* the scale's keys */
        const uint32_t ap = pool_abs();
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    }
    plat_glow(0);
    plat_play_red(en_seq_rec);
    plat_leds(b, k);
}

static void de_autosave(void)                    /* the key and the tempo, kept: quietly, never while playing */
{
    if (ui.dirty && !en_seq_playing && !de.btn && !de.keys && plat_ms() - de.act_t > 3000u && en_pad_level < 0.01f)
        if (project_save() == 0)
            ui.dirty = 0;
}

static void de_frame(void)
{
    de_input();
    de_autosave();
    de_draw_header();
    de_draw_main();
    de_draw_foot();
    de_leds();
    ann_tick(plat_ms());
}
