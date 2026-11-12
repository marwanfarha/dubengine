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

enum { DE_PLAY, DE_PATTERN, DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2, DE_L1, DE_L2, DE_DELAY, DE_REVERB, DE_REVERB2, DE_NPAGE };
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
    {SY_RDECAY, SY_RSIZE, SY_RPRE, SY_RMIX, SY_RWIDTH, SY_RHICUT},
    {SY_RDAMP, SY_RDIFF, SY_RMODD, SY_RMODR, SY_RLOW, SY_RFREEZE},
};
static const char *const DE_PAGE_NAME[DE_NPAGE] = {"PLAY", "PATTERN", "OSC", "FILTER", "AMP ENV", "FILTER ENV", "ENV 1",
                                                   "ENV 2", "LFO 1", "LFO 2", "DELAY", "REVERB", "REVERB 2"};
static const uint8_t DE_ENV_CYCLE[6] = {DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2};
static int de_sound_page(int p) { return p >= DE_OSC && p < DE_NPAGE; }
static int de_kid(int pg, int i)                 /* a sound page's control i: the Delay's Time the free one in Free */
{
    if (pg == DE_DELAY && i == 0 && !en_syn[SY_DSYNC])
        return SY_DMS;
    return DE_KNOBS[pg][i];
}
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
    int8_t touch;                                /* the control last turned (0..3 the knobs, 4 ALGORITHM, 5 PRESETS,
                                                  * 6 SELECT), -1 none; and when */
    uint32_t touch_t;
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

static int de_len(const char *s)                 /* (the device's libc has no strlen) */
{
    int n = 0;
    while (s[n])
        n++;
    return n;
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

/* ---------------------------------------------------------------- saving --- */
/* The pattern and the sound, kept in flash (OBJ_DE, storage.c: A/B copies, a torn write leaves the last one). The key and
 * the tempo are the project's (OBJ_PROJ). Saved with GLO held, and by itself once things are quiet: stopped, nothing
 * touched for 3 s, nothing ringing (a save stops the audio for a moment while the flash is erased). */
#define DE_SAVE_MAGIC 0x474E4544u                /* "DENG" */
#define DE_SAVE_VER 1u
typedef struct {
    uint32_t magic;
    uint16_t ver, nsyn;                          /* (nsyn: the sound's parameters kept; a later firmware reads what's there) */
    uint8_t len, div, metro_on, metro_lvl, voicing, rsv[3];
    uint8_t step[EN_SEQ_MAX][5];                 /* on, degree, octave, voicing, length */
    int16_t syn[SY_N];
} de_save_t;
static de_save_t de_sv;                          /* (the state as last read: saving and comparing) */
static uint32_t de_saved_sum;

static void de_gather(de_save_t *v)
{
    int i;
    memset(v, 0, sizeof *v);
    v->magic = DE_SAVE_MAGIC, v->ver = DE_SAVE_VER, v->nsyn = SY_N;
    v->len = en_seq_len, v->div = en_seq_div, v->metro_on = en_metro_on, v->metro_lvl = en_metro_lvl, v->voicing = de.voicing;
    for (i = 0; i < EN_SEQ_MAX; i++) {
        v->step[i][0] = en_seq[i].on, v->step[i][1] = en_seq[i].deg, v->step[i][2] = en_seq[i].oct;
        v->step[i][3] = en_seq[i].voi, v->step[i][4] = en_seq[i].len;
    }
    for (i = 0; i < SY_N; i++)
        v->syn[i] = en_syn[i];
}
static uint32_t de_sum(const de_save_t *v)       /* (has anything changed since the last save) */
{
    const uint8_t *b = (const uint8_t *)v;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *v; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static int de_save(void)                         /* 0: saved */
{
    int rc;
    de_gather(&de_sv);
    rc = plat_store_save(OBJ_DE, &de_sv, sizeof de_sv);
    if (rc == 0)
        de_saved_sum = de_sum(&de_sv);
    if (ui.dirty && project_save() == 0)         /* (the key and the tempo) */
        ui.dirty = 0;
    return rc;
}
static void de_load(void)                         /* the last saved pattern and sound, to the engine */
{
    const int n = plat_store_load(OBJ_DE, &de_sv, sizeof de_sv);
    int i;
    if (n < (int)(sizeof de_sv - sizeof de_sv.syn) || de_sv.magic != DE_SAVE_MAGIC || de_sv.ver != DE_SAVE_VER)
        return;                                  /* (nothing saved yet: the defaults) */
    en_seq_length(de_sv.len);
    en_seq_division(de_sv.div);
    en_metro(de_sv.metro_on, de_sv.metro_lvl);
    de.voicing = (uint8_t)(de_sv.voicing < 3 ? de_sv.voicing : EN_VO_FIRST);
    en_voicing_set(de.voicing);
    for (i = 0; i < EN_SEQ_MAX; i++) {
        en_step_t t;
        t.on = de_sv.step[i][0], t.deg = de_sv.step[i][1], t.oct = de_sv.step[i][2], t.voi = de_sv.step[i][3];
        t.len = de_sv.step[i][4];
        en_seq_step(i, &t);
    }
    for (i = 0; i < SY_N && i < de_sv.nsyn && (int)(sizeof de_sv - sizeof de_sv.syn + 2u * (uint32_t)(i + 1)) <= n; i++)
        en_syn_set(i, de_sv.syn[i]);
}

static void de_init(void)
{
    int i;
    memset(&de, 0, sizeof de);
    de.page = DE_PLAY;
    de.touch = -1;
    de.oct = 1;
    de.step = -1;
    de.voicing = EN_VO_FIRST;
    de.last.on = 1, de.last.deg = 0, de.last.oct = 2, de.last.voi = EN_VO_KEY, de.last.len = 1;
    for (i = 0; i < NB; i++)
        de.held_done[i] = 0;
    en_voicing_set(de.voicing);
    en_metro(0, 60);
    de_load();
    de_saved_sum = 0;                            /* (compared once the engine has it all: de_autosave) */
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
    case B_FX:                                   /* Delay, Reverb, Reverb 2, in turn */
        de.page = de.page == DE_DELAY ? DE_REVERB : de.page == DE_REVERB ? DE_REVERB2 : DE_DELAY;
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
        ui.dirty = 1;
        de_say(de_save() == 0 ? "SAVED" : "SAVE FAILED");
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
    case SY_RFREEZE: de_cat(b, v ? "On" : "Off"); return;
    case SY_DSYNC: de_cat(b, v ? "Sync" : "Free"); return;
    case SY_DMS: {
        const int ms = (int)(en_delay_ms(v) + 0.5f);
        de_num(b, ms < 1000 ? ms : ms / 10 * 10);
        de_cat(b, "ms");
        return;
    }
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
        de_param_turn(de_kid(de.page, k), d);
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
            de_knob((int)i, (int)d), de.act_t = de.touch_t = now, de.touch = (int8_t)i;
    }
    {   /* ALGORITHM and PRESETS: on Env 1, Env 2 the target and the depth; on the LFOs the sync */
        const int32_t da = plat_enc(EN_ALGO), dp = plat_enc(EN_PRESET);
        if (de_sound_page(de.page) && !(de.edit && de.step >= 0)) {
            if (da)
                de_param_turn(de_kid(de.page, 4), (int)da), de.act_t = de.touch_t = now, de.touch = 4;
            if (dp)
                de_param_turn(de_kid(de.page, 5), (int)dp), de.act_t = de.touch_t = now, de.touch = 5;
        }
    }
    {   /* SELECT: on the Delay page, Sync or Free */
        const int32_t ds = plat_enc(EN_SELECT);
        if (ds && de.page == DE_DELAY && !(de.edit && de.step >= 0)) {
            en_syn_set(SY_DSYNC, ds > 0 ? 0 : 1);
            en_syn[SY_DSYNC] = (int16_t)(ds > 0 ? 0 : 1);
            de.act_t = de.touch_t = now, de.touch = 6;
        }
    }
    if (btn || keys || ch)
        de.act_t = now;
    midi_in();
}

/* ---------------------------------------------------------------- screen --- */
/* Black and orange, after Elektron's screens: one accent (orange) for what's live or touched, white for values, grey for
 * labels and scales, the Terminus bitmap font. Three bands: the header (the page, the key, the transport, the tempo),
 * the middle (the chord and the pattern, or the page's picture), the footer (the four knobs, each with its value's bar).
 * Every picture is drawn to scale from the values as heard: the LFO across a bar, the envelopes on a time axis, the
 * oscillators as they sound (detune and fold, modulation included), the filter's curve where the cutoff is now, the
 * delay's repeats and the reverb's tail in milliseconds and seconds. */
#define DE_HDR 24
#define DE_MAIN 144
#define DE_FOOT (240 - DE_HDR - DE_MAIN)
#define DE_BG RGB(0, 0, 0)
#define DE_FG RGB(236, 236, 236)
#define DE_DIM RGB(120, 120, 120)
#define DE_LINE RGB(44, 44, 44)
#define DE_GRID RGB(26, 26, 26)
#define DE_OR RGB(255, 112, 16)                  /* the accent: live, touched, on */
#define DE_ORD RGB(110, 46, 8)                   /* (and its dark: a range, a track) */
#define DE_TOUCH_MS 1200u

static void de_upper(char *d, const char *s, int n)   /* (labels in capitals, as the panel prints them) */
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
static int de_touched(int i) { return de.touch == i && plat_ms() - de.touch_t < DE_TOUCH_MS; }

static void de_draw_header(void)
{
    char b[24], t[16];
    int32_t x, w;
    const int edit = de.edit && de.step >= 0;
    cv_begin(240, DE_HDR, DE_BG);
    de_upper(t, edit ? "STEP" : DE_PAGE_NAME[de.page], sizeof t);
    w = text_w(&FONT_T_B, t) + 8;
    cv_rect(0, 2, w, 19, DE_OR);                 /* (the page, inverted) */
    cv_text(4, 3, &FONT_T_B, t, DE_BG);
    b[0] = 0;
    de_cat(b, EN_NOTE_NAME[proj.par[P_ROOT] % 12]);
    de_cat(b, " ");
    de_upper(t, EN_MODE_NAME[proj.par[P_MODE] % EN_NMODES], 4);
    de_cat(b, t);
    x = cv_text(w + 6, 4, &FONT_T_S, b, DE_FG);
    /* the transport, at the right: the tempo; the click; record (or edit); play */
    de_num(b, proj.par[P_TEMPO]);
    w = text_w(&FONT_T_B, b);
    cv_text(238 - w, 3, &FONT_T_B, b, DE_FG);
    x = 238 - w - 14;
    if (en_metro_on) {                           /* (the click: a small metronome, two bars) */
        cv_rect(x + 2, 6, 2, 12, DE_OR);
        cv_rect(x + 6, 10, 2, 8, DE_OR);
    }
    x -= 14;
    if (de.edit) {
        cv_rect(x, 6, 10, 12, DE_OR);
        cv_text(x + 2, 4, &FONT_T_XS, "E", DE_BG);
    } else {                                      /* (record: a dot, orange while recording) */
        const uint16_t c = en_seq_rec ? DE_OR : DE_LINE;
        cv_rect(x + 2, 8, 6, 8, c);
        cv_rect(x + 1, 9, 8, 6, c);
    }
    x -= 14;
    {   /* (play: a triangle, orange while playing) */
        const uint16_t c = en_seq_playing ? DE_OR : DE_DIM;
        int k;
        for (k = 0; k < 6; k++)
            cv_rect(x + 2 + k, 6 + k, 1, 12 - 2 * k, c);
    }
    cv_rect(0, DE_HDR - 1, 240, 1, DE_LINE);
    cv_blit(0, 0);
}

/* ---- the pictures: helpers */
static float de_sin(float t)                     /* sin(2 pi t) */
{
    t -= (float)(int)t;
    if (t < 0.0f)
        t += 1.0f;
    return fm_sin_turns(t);
}
static float de_osc1(int w, float t)             /* one oscillator, -1..1, t in turns */
{
    t -= (float)(int)t;
    if (t < 0.0f)
        t += 1.0f;
    return w == EN_WAVE_SINE ? de_sin(t) : w == EN_WAVE_TRI ? (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t)
           : w == EN_WAVE_SQUARE ? (t < 0.5f ? 1.0f : -1.0f) : 2.0f * t - 1.0f;
}
static float de_lshape(int sh, float t, int cyc)   /* an LFO's shape, -1..1 (random: a held value a cycle) */
{
    static const float RND[16] = {0.62f, -0.35f, 0.91f, -0.78f, 0.12f, -0.55f, 0.43f, -0.08f,
                                  -0.92f, 0.27f, 0.71f, -0.21f, 0.05f, -0.66f, 0.84f, -0.44f};
    return sh == EN_LS_TRI ? (t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t) : sh == EN_LS_SAW ? 2.0f * t - 1.0f
           : sh == EN_LS_SQUARE ? (t < 0.5f ? 1.0f : -1.0f) : sh == EN_LS_RANDOM ? RND[cyc & 15] : de_sin(t);
}
static void de_plot(int x0, int y0, int w, int h, const float *v, int n, uint16_t c)   /* v[0..n): -1..1, a line */
{
    int i, py = 0;
    for (i = 0; i < n; i++) {
        const float f = fm_clampf(v[i], -1.0f, 1.0f);
        const int y = y0 + h / 2 - (int)(f * (float)(h / 2 - 1));
        if (i)
            cv_line(x0 + (i - 1) * w / (n - 1), py, x0 + i * w / (n - 1), y, c);
        py = y;
    }
}
static void de_axis(int x0, int y0, int w, float span, float step, const char *unit)   /* ticks and labels, 0..span */
{
    float t;
    char b[12];
    for (t = 0.0f; t <= span + 0.0001f; t += step) {
        const int x = x0 + (int)((float)w * t / span);
        cv_rect(x, y0, 1, 3, DE_DIM);
        if (t > 0.0f && x < x0 + w - 8) {
            b[0] = 0;
            if (step < 1.0f)
                de_num(b, (int)(t * 1000.0f + 0.5f));
            else
                de_num(b, (int)(t + 0.5f));
            de_cat(b, step < 1.0f ? "" : unit);
            cv_text(x - text_w(&FONT_T_XS, b) / 2, y0 + 3, &FONT_T_XS, b, DE_DIM);
        }
    }
}
static float de_bar_s(void) { return 240.0f / (float)(proj.par[P_TEMPO] > 20 ? proj.par[P_TEMPO] : 120); }   /* a bar, s */
static float de_dtime_s(void)                    /* the delay's time, s */
{
    static const float BEATS[EN_NDTIME] = {0.25f, 0.3333f, 0.5f, 0.75f, 0.6667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    if (!en_syn[SY_DSYNC])
        return en_delay_ms(en_syn[SY_DMS]) * 0.001f;
    return BEATS[en_syn[SY_DTIME] % EN_NDTIME] * de_bar_s() * 0.25f;
}

#define PX 8                                     /* the picture's box */
#define PY 22
#define PW 224
static int de_ph = 96;                           /* (96; 84 when the extras' line sits under it) */
#define PH de_ph
static void de_pic_osc(void)                     /* the two oscillators as they sound: detuned, folded, modulated now */
{
    float v[113], det = en_det_now, fold = en_fold_now, beat;
    const int w = en_syn[SY_WAVE];
    int i;
    beat = (float)(plat_ms() % 100000u) * 0.001f * (0.6f + 3.0f * det);   /* (the detune's beating, slowed to be seen) */
    for (i = 0; i < 113; i++) {
        const float t = 2.0f * (float)i / 112.0f;   /* (two cycles) */
        const float x = de_osc1(w, t) + de_osc1(w, t * (1.0f + 0.0145f * det * 2.0f) + beat);
        v[i] = 1.35f * de_sin(x * (0.0625f + 0.8125f * fold) + 4.0f);   /* (at Fold 0, its peak 0.71: shown full) */
    }
    cv_rect(PX, PY + PH / 2, PW, 1, DE_GRID);
    de_plot(PX, PY, PW, PH, v, 113, DE_OR);
}
static float de_lp_db(float f, float fc, float k)   /* the 2-pole low-pass at f, dB */
{
    const float r = f / fc, a = 1.0f - r * r, b = k * r, g = 1.0f / fm_sqrtf(a * a + b * b);
    return 6.0206f * fm_log2f(g > 1e-6f ? g : 1e-6f);
}
static void de_pic_filter(void)                  /* the response, 20 Hz .. 20 kHz: the knobs' (grey) and as heard now (orange) */
{
    const float fc0 = 40.0f * fm_exp2f((float)en_syn[SY_CUT] * 0.0864f), k0 = 2.0f - 1.9f * (float)en_syn[SY_RESO] * 0.01f,
                k1 = 2.0f - 1.9f * fm_clampf((float)en_syn[SY_RESO] * 0.01f + en_md_now[EN_MD_RESO], 0.0f, 1.0f),
                fc1 = en_cut_now > 0.0f ? en_cut_now : fc0;
    static const float TICK[3] = {100.0f, 1000.0f, 10000.0f};
    static const char *const TN[3] = {"100", "1K", "10K"};
    int i, p0 = 0, p1 = 0;
    for (i = 0; i < 3; i++) {
        const int x = PX + (int)((float)PW * fm_log2f(TICK[i] / 20.0f) / 9.966f);
        cv_rect(x, PY, 1, PH, DE_GRID);
        if (x + 2 + text_w(&FONT_T_XS, TN[i]) <= PX + PW)
            cv_text(x + 2, PY + PH - 12, &FONT_T_XS, TN[i], DE_DIM);
    }
    cv_rect(PX, PY + 20, PW, 1, DE_GRID);         /* (0 dB) */
    for (i = 0; i <= PW; i++) {
        const float f = 20.0f * fm_exp2f(9.966f * (float)i / (float)PW);
        int y0 = PY + 20 - (int)(de_lp_db(f, fc0, k0) * 1.4f), y1 = PY + 20 - (int)(de_lp_db(f, fc1, k1) * 1.4f);
        y0 = y0 < PY ? PY : y0 > PY + PH ? PY + PH : y0;
        y1 = y1 < PY ? PY : y1 > PY + PH ? PY + PH : y1;
        if (i) {
            cv_line(PX + i - 1, p0, PX + i, y0, DE_DIM);
            cv_line(PX + i - 1, p1, PX + i, y1, DE_OR);
        }
        p0 = y0, p1 = y1;
    }
}
static void de_pic_env(int e)                    /* a 1 s note's envelope (the gate at 1 s), 0..3 s; where it is now */
{
    static const uint8_t A[4] = {SY_AA, SY_FA, SY_1A, SY_2A};
    const int a = A[e];
    const float ta = 0.001f * fm_exp2f((float)en_syn[a] * 0.1329f), td = 0.001f * fm_exp2f((float)en_syn[a + 1] * 0.1329f),
                sus = (float)en_syn[a + 2] * 0.01f, tr = 0.001f * fm_exp2f((float)en_syn[a + 3] * 0.1329f),
                span = 3.0f, dt = span / (float)PW;
    float lv = 0.0f, t = 0.0f;
    int st = 1, i, py = PY + PH, xg = PX + (int)((float)PW / span);
    for (i = 0; i <= PW; i++, t += dt) {
        int y;
        if (t >= 1.0f && st != 3)
            st = 3;                              /* (the gate off at 1 s) */
        if (st == 1) {
            lv += dt / ta;
            if (lv >= 1.0f)
                lv = 1.0f, st = 2;
        } else if (st == 2) {
            lv = sus + (lv - sus) * fm_expf(-dt * 6.9f / td);
        } else {
            lv *= fm_expf(-dt * 6.9f / tr);
        }
        y = PY + PH - (int)(lv * (float)(PH - 4));
        if (i)
            cv_line(PX + i - 1, py, PX + i, y, DE_OR);
        py = y;
    }
    cv_rect(xg, PY, 1, PH, DE_LINE);             /* (the note off) */
    cv_text(xg + 3, PY, &FONT_T_XS, "OFF", DE_DIM);
    cv_rect(PX, PY + PH, PW, 1, DE_LINE);
    de_axis(PX, PY + PH + 1, PW, span, 1.0f, "S");
    if (en_env_st[e]) {                          /* (now: its level, where in the note it is) */
        float tn = en_env_age[e] + (en_env_st[e] == 2 ? ta : 0.0f);
        int x, y;
        if (en_env_st[e] == 3)
            tn = 1.0f + en_env_age[e];
        x = PX + (int)((float)PW * fm_minf(tn, span) / span);
        y = PY + PH - (int)(en_env_lv[e] * (float)(PH - 4));
        cv_rect(x - 2, y - 2, 5, 5, DE_FG);
    }
}
static void de_pic_lfo(int k)                    /* a bar of the LFO, its depth the height; where it is now */
{
    const int rate = en_syn[k ? SY_L2RATE : SY_L1RATE], sh = en_syn[k ? SY_L2SHAPE : SY_L1SHAPE],
              sync = en_syn[k ? SY_L2SYNC : SY_L1SYNC], dp = en_syn[k ? SY_L2AMT : SY_L1AMT];
    static const float SB[EN_NSYNC] = {0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f};
    const float bar = de_bar_s(), cyc = sync > 0 ? 4.0f / SB[sync % EN_NSYNC] : 0.02f * fm_exp2f((float)rate * 0.0997f) * bar,
                d = (float)dp * 0.01f;
    float v[225];
    int i, n = 225;
    for (i = 0; i < n; i++) {
        const float t = cyc * (float)i / (float)(n - 1), ph = t - (float)(int)t;
        v[i] = d * de_lshape(sh, ph, (int)t);
    }
    for (i = 1; i < 4; i++)                      /* (the beats) */
        cv_rect(PX + i * PW / 4, PY, 1, PH, DE_GRID);
    cv_rect(PX, PY + PH / 2, PW, 1, DE_LINE);
    de_plot(PX, PY, PW, PH, v, n, DE_OR);
    if (cyc < 32.0f) {                           /* (now: on its first cycle) */
        const float ph = en_lfo_ph[k];
        const int x = PX + (int)((float)PW * ph / fm_maxf(cyc, 1.0f)),
                  y = PY + PH / 2 - (int)(d * de_lshape(sh, ph, 0) * (float)(PH / 2 - 1));
        if (cyc >= 1.0f)
            cv_rect(x - 2, y - 2, 5, 5, DE_FG);
    }
    cv_text(PX + PW - text_w(&FONT_T_XS, "1 BAR"), PY + PH + 3, &FONT_T_XS, "1 BAR", DE_DIM);
}
static void de_pic_delay(void)                   /* the repeats in seconds: each as Feedback leaves it, the first as Mix sends */
{
    const float ts = de_dtime_s(), fb = fm_minf((float)en_syn[SY_DFB] * 0.01f, 1.0f), mx = (float)en_syn[SY_DMIX] * 0.01f,
                span = fm_clampf(ts * 6.0f, 1.0f, 6.0f), wow = (float)en_syn[SY_DWOW] * 0.01f;
    float lv = fm_minf(2.0f * mx, 1.0f), t;
    const int hd = (int)((float)(PH - 4) * fm_minf(2.0f * (1.0f - mx), 1.0f));
    int n = 0;
    char b[16];
    cv_rect(PX, PY + PH, PW, 1, DE_LINE);
    if (hd > 0)
        cv_rect(PX, PY + PH - hd, 3, hd, DE_FG);    /* (the dry) */
    for (t = ts; t < span && n < 60; t += ts, n++) {
        const int x = PX + (int)((float)PW * (t + wow * 0.01f * de_sin((float)n * 0.37f)) / span),
                  hh = (int)((float)(PH - 4) * lv);
        if (hh > 0)
            cv_rect(x, PY + PH - hh, 3, hh, n ? mix(DE_ORD, DE_OR, 16 - (int)(10.0f * (100.0f - (float)en_syn[SY_DTONE]) * 0.01f)) : DE_OR);
        lv *= fb;
    }
    de_axis(PX, PY + PH + 1, PW, span, span > 2.5f ? 1.0f : 0.5f, "S");
    b[0] = 0;
    de_num(b, (int)(ts * 1000.0f + 0.5f));
    de_cat(b, " MS");
    cv_text(PX + PW - text_w(&FONT_T_XS, b), PY, &FONT_T_XS, b, DE_DIM);
}
static float de_rt60(int size, int decay)       /* the plate's tail to -60 dB, s: measured (tests' rt grid), between */
{
    static const float SZ[3] = {0.0f, 50.0f, 100.0f}, DC[5] = {0.0f, 30.0f, 55.0f, 80.0f, 100.0f};
    static const float RT[3][5] = {{1.0f, 1.2f, 1.8f, 3.7f, 11.5f}, {1.4f, 2.3f, 3.5f, 6.5f, 23.2f}, {2.4f, 3.4f, 5.0f, 10.9f, 30.0f}};
    int i = size >= 50 ? 1 : 0, j = 0;
    float u, v, a, b;
    while (j < 3 && (float)decay > DC[j + 1])
        j++;
    u = ((float)size - SZ[i]) / 50.0f;
    v = ((float)decay - DC[j]) / (DC[j + 1] - DC[j]);
    a = fm_log2f(RT[i][j]) + (fm_log2f(RT[i][j + 1]) - fm_log2f(RT[i][j])) * v;
    b = fm_log2f(RT[i + 1][j]) + (fm_log2f(RT[i + 1][j + 1]) - fm_log2f(RT[i + 1][j])) * v;
    return fm_exp2f(a + (b - a) * u);
}
static void de_pic_reverb(void)                  /* the tail in seconds: the pre-delay's gap, then the decay as Mix sends it */
{
    const float mx = fm_minf(2.0f * (float)en_syn[SY_RMIX] * 0.01f, 1.0f),
                rt = de_rt60(en_syn[SY_RSIZE], en_syn[SY_RDECAY]), pre = (1.0f + (float)en_syn[SY_RPRE]) * 0.001f,
                span = fm_clampf(rt * 1.2f, 1.0f, 8.0f);
    int i, x0 = PX + (int)((float)PW * pre / span);
    char b[16];
    cv_rect(PX, PY + PH, PW, 1, DE_LINE);
    cv_rect(PX, PY + 4, 3, PH - 4, DE_FG);        /* (the dry) */
    for (i = x0; i < PX + PW; i++) {
        const float t = (float)(i - x0) * span / (float)PW, a = mx * fm_exp2f(-t * 9.966f / rt);
        const int hh = (int)((float)(PH - 4) * a);
        if (hh > 0)
            cv_rect(i, PY + PH - hh, 1, hh, (i & 1) ? DE_ORD : DE_OR);
    }
    de_axis(PX, PY + PH + 1, PW, span, span > 3.0f ? 1.0f : 0.5f, "S");
    b[0] = 0;
    de_cat(b, "RT60 ");
    de_num(b, (int)rt);
    de_cat(b, ".");
    de_num(b + de_len(b), (int)(rt * 10.0f) % 10);
    de_cat(b, " S");
    cv_text(PX + PW - text_w(&FONT_T_XS, b), PY, &FONT_T_XS, b, DE_DIM);
}

static void de_touch_text(char *lab, char *val)  /* the control last turned: its name and value (for the big readout) */
{
    const int id = de.touch < 6 ? de_kid(de.page, de.touch) : SY_DSYNC;
    lab[0] = val[0] = 0;
    if (de.touch == 6) {
        de_upper(lab, "Delay mode", 16);
        de_param_text(SY_DSYNC, val);
        return;
    }
    if (de_sound_page(de.page)) {
        de_upper(lab, de_param_name(id), 16);
        de_param_text(id, val);
    }
}

static void de_draw_main(void)
{
    char b[24], l[16];
    const uint32_t now = plat_ms();
    const int msg = de.msg_t && now - de.msg_t < DE_MSG_MS, held = de.edit && de.step >= 0;
    int i, rows, n = de_nsteps();
    cv_begin(240, DE_MAIN, DE_BG);
    if (de_sound_page(de.page) && !held) {
        /* the top line: a message, or the control being turned, large; else the chord */
        if (msg) {
            cv_text(8, 0, &FONT_T_B, de.msg, DE_OR);
        } else if (de.touch >= 0 && de_touched(de.touch)) {
            de_touch_text(l, b);
            de_upper(b, b, 24);
            cv_text(8, 0, &FONT_T_B, l, DE_OR);
            cv_text(232 - text_w(&FONT_T_B, b), 0, &FONT_T_B, b, DE_FG);
        } else {
            b[0] = 0;
            if (en_now_n) {
                de_cat(b, EN_NOTE_NAME[en_now_pc % 12]);
                de_cat(b, DE_Q_NAME[en_now_q <= EN_Q_OTHER ? en_now_q : EN_Q_OTHER]);
            }
            cv_text(8, 0, &FONT_T_B, b, DE_FG);
        }
        de_ph = (de_kid(de.page, 4) >= 0 || de_kid(de.page, 5) >= 0 || de.page == DE_DELAY) ? 84 : 96;
        switch (de.page) {
        case DE_OSC: de_pic_osc(); break;
        case DE_FILT: de_pic_filter(); break;
        case DE_AENV: de_pic_env(0); break;
        case DE_FENV: de_pic_env(1); break;
        case DE_E1: de_pic_env(2); break;
        case DE_E2: de_pic_env(3); break;
        case DE_L1: de_pic_lfo(0); break;
        case DE_L2: de_pic_lfo(1); break;
        case DE_DELAY: de_pic_delay(); break;
        default: de_pic_reverb(); break;
        }
        /* the extra controls: ALGORITHM, PRESETS (and the Delay's SELECT), on one line */
        {
            static const char *const EN[3] = {"ALG", "PRE", "SEL"};
            int32_t x = 8;
            for (i = 0; i < 3; i++) {
                const int id = i < 2 ? de_kid(de.page, 4 + i) : de.page == DE_DELAY ? SY_DSYNC : -1;
                if (id < 0)
                    continue;
                x = cv_text(x, DE_MAIN - 16, &FONT_T_XS, EN[i], de_touched(4 + i) ? DE_OR : DE_DIM) + 4;
                b[0] = 0;
                if (id != SY_DSYNC) {
                    de_upper(l, de_param_name(id), 12);
                    de_cat(b, l);
                    de_cat(b, " ");
                }
                de_param_text(id, b + de_len(b));
                de_upper(b, b, 24);
                x = cv_text(x, DE_MAIN - 16, &FONT_T_XS, b, DE_FG) + 10;
            }
        }
        cv_blit(0, DE_HDR);
        return;
    }
    /* the Play and Pattern pages: the chord (the step held's in step edit), large; the pattern under it */
    if (held) {
        en_step_t t;
        de_step_get(de.step, &t);
        de_chord_name(&t, b);
    } else if (msg) {
        for (i = 0; de.msg[i]; i++)
            b[i] = de.msg[i];
        b[i] = 0;
    } else {
        b[0] = 0;
        if (en_now_n) {
            de_cat(b, EN_NOTE_NAME[en_now_pc % 12]);
            de_cat(b, DE_Q_NAME[en_now_q <= EN_Q_OTHER ? en_now_q : EN_Q_OTHER]);
        } else {
            de_cat(b, "DUBENGINE");
        }
    }
    cv_text((240 - text_w(&FONT_T_C, b)) / 2, 14, &FONT_T_C, b, msg ? DE_OR : DE_FG);
    /* the pattern: 16 a row, up to 4 rows; steps with a chord orange, the beats' first steps lighter, the playhead white */
    rows = (n + 15) / 16;
    for (i = 0; i < rows * 16; i++) {
        const int r = i / 16, c = i % 16, x = 8 + c * 14, y = 56 + r * 20;
        uint16_t col;
        if (i >= n)
            continue;
        if (en_seq[i].on)
            col = mix(DE_ORD, DE_OR, 8 + 8 * (int)en_seq[i].deg / 6);
        else
            col = (c & 3) ? DE_GRID : DE_LINE;
        if (en_seq_playing && i == en_seq_pos)
            col = DE_FG;
        cv_rect(x, y, 12, 14, col);
        if (en_seq[i].on && en_seq[i].len > 1) {     /* (a held chord: a line to its end, on its row) */
            int e = c + en_seq[i].len - 1;
            e = e > 15 ? 15 : e;
            cv_rect(x + 12, y + 6, (e - c) * 14, 2, DE_ORD);
        }
        if (de.edit && r == de.spage)
            cv_rect(x, y + 16, 12, 2, DE_OR);        /* (the step page the keys edit) */
        if (de.edit && i == de.step)
            cv_rect(x, y - 4, 12, 2, DE_FG);
    }
    cv_blit(0, DE_HDR);
}

static int de_bar_frac(int id, int *bip)         /* a control's place in its range, 0..1000 (bipolar: -1000..1000) */
{
    int lo, hi, v;
    *bip = 0;
    if (id < 0)
        return 0;
    if (id >= DE_P) {
        const en_param_t *pi = en_param_info(id - DE_P);
        lo = pi ? pi->lo : 0, hi = pi ? pi->hi : 100;
        v = proj.par[id - DE_P];
    } else {
        lo = SYN_INFO[id].lo, hi = SYN_INFO[id].hi;
        v = en_syn[id];
    }
    if (lo < 0 && hi > 0) {
        *bip = 1;
        return v * 1000 / (v < 0 ? -lo : hi);
    }
    return hi > lo ? (v - lo) * 1000 / (hi - lo) : 0;
}
static void de_draw_foot(void)
{
    const char *lab[4];
    char val[4][12], up[12];
    int i, id[4] = {-1, -1, -1, -1};
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
            id[i] = de_kid(de.page, i);
            lab[i] = de_param_name(id[i]);
            de_param_text(id[i], val[i]);
        }
    } else if (de.page == DE_PLAY) {
        lab[0] = "Tempo", lab[1] = "Root", lab[2] = "Mode", lab[3] = "Voicing";
        de_num(val[0], proj.par[P_TEMPO]);
        de_cat(val[1], EN_NOTE_NAME[proj.par[P_ROOT] % 12]);
        de_upper(val[2], EN_MODE_NAME[proj.par[P_MODE] % EN_NMODES], 12);
        de_cat(val[3], DE_VO_NAME[de.voicing]);
    } else {
        lab[0] = "Length", lab[1] = "Step", lab[2] = "Click", lab[3] = "";
        de_num(val[0], de_nsteps());
        de_cat(val[1], EN_DIV_NAME[en_seq_div % EN_NDIV]);
        de_num(val[2], en_metro_lvl);
        de_cat(val[2], en_metro_on ? "" : " OFF");
    }
    cv_begin(240, DE_FOOT, DE_BG);
    cv_rect(0, 0, 240, 1, DE_LINE);
    for (i = 0; i < 4; i++) {
        const int32_t x = i * 60, cx = x + 30;
        const int t = de_touched(i);
        int bip, f;
        if (i)
            cv_rect(x, 8, 1, DE_FOOT - 16, DE_LINE);
        de_upper(up, lab[i], sizeof up);
        cv_text(cx - text_w(&FONT_T_XS, up) / 2, 6, &FONT_T_XS, up, t ? DE_OR : DE_DIM);
        de_upper(val[i], val[i], 12);
        cv_text(cx - text_w(&FONT_T_B, val[i]) / 2, 24, &FONT_T_B, val[i], t ? DE_OR : DE_FG);
        if (id[i] >= 0) {                        /* (the value's bar: from the left, or from the middle if bipolar) */
            f = de_bar_frac(id[i], &bip);
            cv_rect(x + 8, 50, 44, 3, DE_LINE);
            if (bip) {
                const int w = f * 22 / 1000;
                if (w >= 0)
                    cv_rect(x + 30, 50, w ? w : 1, 3, DE_OR);
                else
                    cv_rect(x + 30 + w, 50, -w, 3, DE_OR);
            } else {
                cv_rect(x + 8, 50, f * 44 / 1000, 3, DE_OR);
            }
        }
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

static void de_autosave(void)                    /* the pattern, the sound, the key, the tempo, kept: quietly, never
                                                  * while playing */
{
    static uint32_t last_t;
    uint32_t sum, now = plat_ms();
    if (now - last_t < 500u)                     /* (looked at twice a second) */
        return;
    last_t = now;
    de_gather(&de_sv);
    sum = de_sum(&de_sv);
    if (!de_saved_sum && en_queue_left() == 0u && now > 2000u)   /* (after boot: what was loaded is what's saved) */
        de_saved_sum = sum;
    if ((sum == de_saved_sum && !ui.dirty) || en_seq_playing || de.btn || de.keys || now - de.act_t < 3000u ||
        en_pad_level > 0.01f)
        return;
    de_save();
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
