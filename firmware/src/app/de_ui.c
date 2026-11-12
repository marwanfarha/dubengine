/* SPDX-License-Identifier: GPL-3.0-only */
/* dubengine's panel: two tracks' keys, patterns and sounds, the metronome (docs/CONTROLS.md). Included by ui.c (the
 * unity build and the host simulator), whose mix() it uses.
 *
 *   PLAY        start / stop
 *   REC         playing: live recording on / off; stopped: step edit on / off (in step edit: off)
 *   SEL         the metronome on / off
 *   HOME        the Play page: Tempo, Root, Mode, Voicing; again: the other track (A, B)
 *   SEQ         the Pattern page: Length, Step, Click level; held 2.5 s: clear (held again within 5 s: back)
 *   GLO held    save
 *   OCT- / +    the keys' octave; in step edit, the page of 16 steps
 *   keys        lit (in the scale): their chords; in step edit the white keys are the steps: a tap puts the last
 *               chord on a step or takes it off, a step held and a knob turned locks it (Chord, Voicing, Octave,
 *               Length) */

enum { DE_PLAY, DE_PATTERN, DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2, DE_L1, DE_L2, DE_DELAY, DE_REVERB, DE_REVERB2, DE_DELAY2,
       DE_MASTER, DE_EQ, DE_RANDOM, DE_V1, DE_V2, DE_V3, DE_V4, DE_NPAGE };
/* a page's control: a synth parameter (SY_*), a mix parameter (DE_MX + MX_*), the EQ page's band (DE_ID_BAND), a
 * track's level (DE_ID_LVL + the track) */
#define DE_MX 2000
#define DE_ID_BAND 3000
#define DE_ID_LVL 4000
#define DE_VP 6000                               /* (+ VP_*: the bass's or the ping's parameter) */
#define DE_ID_RND 5000                           /* (+ 0..3: the Random page's Density, Variation, Length, Offbeat) */
/* the sound pages: each its four knobs (a synth parameter, SY_*), and ALGORITHM's and PRESETS' (-1 none) */
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
    {SY_DWIDTH, SY_DBLUR, SY_DMODE, SY_DPING, -1, -1},
    {DE_ID_LVL, DE_ID_LVL + 1, DE_ID_LVL + 2, DE_ID_LVL + 3, -1, -1},
    {DE_ID_BAND, -1, -1, -1, -1, -1},            /* (the band's Freq, Gain, Q: de_kid) */
    {DE_ID_RND, DE_ID_RND + 1, DE_ID_RND + 2, DE_ID_RND + 3, -1, -1},
    {DE_VP + VP_VOICE, DE_VP + VP_FM, DE_VP + VP_RATIO, DE_VP + VP_FB, -1, -1},
    {DE_VP + VP_ATT, DE_VP + VP_DEC, DE_VP + VP_MDEC, DE_VP + VP_GLIDE, -1, -1},
    {DE_VP + VP_CUT, DE_VP + VP_DRIVE, DE_VP + VP_LEVEL, DE_VP + VP_REV, -1, -1},
    {DE_VP + VP_DTIME, DE_VP + VP_DFB, DE_VP + VP_DMIX, DE_VP + VP_DTONE, -1, -1},
};
static const char *const DE_PAGE_NAME[DE_NPAGE] = {"PLAY", "PATTERN", "OSC", "FILTER", "AMP ENV", "FILTER ENV", "ENV 1",
                                                   "ENV 2", "LFO 1", "LFO 2", "DELAY", "REVERB", "REVERB 2", "DELAY 2",
                                                   "MASTER", "EQ", "RANDOM", "VOICE", "SHAPE", "TONE", "DELAY"};
static const uint8_t DE_ENV_CYCLE[6] = {DE_OSC, DE_FILT, DE_AENV, DE_FENV, DE_E1, DE_E2};
static int de_sound_page(int p) { return p >= DE_OSC && p < DE_NPAGE; }
static int de_band(void);
static int de_kid(int pg, int i)                 /* a sound page's control i: the Delay's Time the free one in Free */
{
    if (pg == DE_DELAY && i == 0 && !en_syn[SY_DSYNC])
        return SY_DMS;
    if (pg == DE_EQ && i >= 1 && i <= 3) {       /* the band's own: the cuts no gain, only the bells a Q */
        const int b = de_band(), f = i - 1;
        if ((f == EQ_GAIN && (b == EN_EQ_LOCUT || b == EN_EQ_HICUT)) || (f == EQ_Q && (b < EN_EQ_BELL1 || b > EN_EQ_BELL4)))
            return -1;
        return DE_MX + MX_EQ + b * EQ_NF + f;
    }
    return DE_KNOBS[pg][i];
}
#define DE_HOLD_MS 2500u                         /* SEQ held: clear (counted down from 1 s: it is the slots' button too) */
#define DE_UNDO_MS 8000u                         /* (then SEQ held again, begun within 5 s: back) */
#define DE_SAVE_MS 1000u                         /* GLO held: save */
#define DE_MSG_MS 1600u
static const uint8_t DE_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
#define DE_NMODE 7
static const uint8_t DE_MODES[DE_NMODE] = {MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN,
                                           MODE_LOCRIAN};
static const char *const DE_VO_NAME[4] = {"Root", "1st", "2nd", "Key"};
static const char *const DE_Q_NAME[EN_Q_OTHER + 1] = {"", "m", "dim", "aug", "sus"};

static struct {
    uint8_t page, edit, oct, spage;              /* page; step edit; the keys' octave (0..2: -1..+1); step page */
    uint8_t tempo, root[EN_NTRK], mode[EN_NTRK], voi[EN_NTRK];   /* the tempo; each track's key and voicing */
    uint8_t band;                                /* the EQ page's band */
    uint8_t rnd[4];                              /* the Random page: Density, Variation, Length, Offbeat */
    int8_t slot_key;                             /* SEQ or ENV held, a slot's key down: which (0..7), -1 none */
    uint8_t slot_snd, slot_done, slot_hint;      /* (a sound's; saved by the hold; the hint shown) */
    uint32_t slot_t;
    float mpk[EN_NMETER], mhold[EN_NMETER];      /* the Master page's meters: now (falling), the peak held */
    uint32_t mhold_t[EN_NMETER];
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

static int de_band(void) { return de.band < EN_NEQ ? de.band : 0; }
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
static void de_say2(const char *a, const char *b)   /* (main_fm1.c's messages, "UPDATE MODE IN 3..", through ui_say) */
{
    char t[sizeof de.msg];
    uint32_t i = 0, j;
    for (j = 0; a && a[j] && i < sizeof t - 1u; j++)
        t[i++] = a[j];
    if (b && b[0] && i < sizeof t - 1u)
        t[i++] = ' ';
    for (j = 0; b && b[j] && i < sizeof t - 1u; j++)
        t[i++] = b[j];
    t[i] = 0;
    de_say(t);
}
static uint32_t de_pool(void) { return EN_MODE_POOL[de.mode[en_sel] % EN_NMODES]; }   /* the track's scale, from its root */
static uint32_t de_pool_abs(void)                /* (and as pitch classes, C = bit 0) */
{
    const uint32_t p = de_pool(), r = de.root[en_sel] % 12u;
    return (p << r | p >> (12u - r)) & 0xFFFu;
}
static void de_chord_name(const en_step_t *st, char *b)   /* "Cm", "Ddim", "Eb" */
{
    int s[12], m = 0, i, pc, t1, t2, q;
    for (i = 0; i < 12; i++)
        if (de_pool() >> i & 1u)
            s[m++] = i;
    if (m < 3) {
        b[0] = '?', b[1] = 0;
        return;
    }
    pc = (de.root[en_sel] + s[st->deg % m]) % 12;
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
static void de_spage_set(int p)                  /* step edit's page of 16 steps, and "STEPS 17 TO 32" */
{
    static const char *const T[4] = {"STEPS 1 TO 16", "STEPS 17 TO 32", "STEPS 33 TO 48", "STEPS 49 TO 64"};
    const int n = de_spages();
    p = p < 0 ? 0 : p >= n ? n - 1 : p;
    de.spage = (uint8_t)p;
    de_say(T[p & 3]);
}

/* ---------------------------------------------------------------- saving --- */
/* Everything, kept in flash (OBJ_DE, storage.c: A/B copies, a torn write leaves the last one): the tempo, the click, and
 * each track's key, pattern and sound. Saved with GLO held, and by itself once things are quiet: stopped, nothing
 * touched for 3 s, nothing ringing (a save stops the audio for a moment while the flash is erased). */
#define DE_SAVE_MAGIC 0x474E4544u                /* "DENG" */
#define DE_SAVE_VER 4u                           /* (4: the bass and the ping) */
#define DE_SAVE_VER_3 3u                           /* (2: two tracks, each its key; 3: the steps' locks, the level and the EQ) */
typedef struct {
    uint8_t len, div, voicing, root, mode, rsv[3];
    uint8_t step[EN_SEQ_MAX][8];                 /* on, degree, octave, voicing, length, its three locks */
    int16_t syn[SY_N];
    int8_t mx[MX_N], vp[VP_N];
} de_trk_save_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, nsyn;                          /* (nsyn: the sound's parameters kept) */
    uint8_t tempo, metro_on, metro_lvl, sel;
    uint8_t rnd[4];
    de_trk_save_t trk[EN_NTRK];
} de_save_t;
_Static_assert(sizeof(de_save_t) <= PLAT_STORE_MAX, "the save fits its flash object");
static de_save_t de_sv;                          /* (the state as last read: saving and comparing) */
static uint32_t de_saved_sum;

static void de_gather(de_save_t *v)
{
    int i, t;
    memset(v, 0, sizeof *v);
    v->magic = DE_SAVE_MAGIC, v->ver = DE_SAVE_VER, v->nsyn = SY_N;
    v->tempo = de.tempo, v->metro_on = en_metro_on, v->metro_lvl = en_metro_lvl, v->sel = en_sel;
    for (i = 0; i < 4; i++)
        v->rnd[i] = de.rnd[i];
    for (t = 0; t < EN_NTRK; t++) {
        de_trk_save_t *k = &v->trk[t];
        k->len = en_trk[t].seq_len, k->div = en_trk[t].seq_div, k->voicing = de.voi[t], k->root = de.root[t], k->mode = de.mode[t];
        for (i = 0; i < EN_SEQ_MAX; i++) {
            k->step[i][0] = en_trk[t].seq[i].on, k->step[i][1] = en_trk[t].seq[i].deg, k->step[i][2] = en_trk[t].seq[i].oct;
            k->step[i][3] = en_trk[t].seq[i].voi, k->step[i][4] = en_trk[t].seq[i].len;
            k->step[i][5] = en_trk[t].seq[i].lock[0], k->step[i][6] = en_trk[t].seq[i].lock[1], k->step[i][7] = en_trk[t].seq[i].lock[2];
        }
        for (i = 0; i < SY_N; i++)
            k->syn[i] = en_trk[t].syn[i];
        for (i = 0; i < MX_N; i++)
            k->mx[i] = (int8_t)en_trk[t].mx[i];
        for (i = 0; i < VP_N; i++)
            k->vp[i] = (int8_t)en_trk[t].vp[i];
    }
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
    return rc;
}
static void de_key_send(int t)                   /* track t's key and voicing, to the engine */
{
    const uint8_t was = en_sel;
    en_sel = (uint8_t)t;
    en_set(P_ROOT, de.root[t]);
    en_set(P_MODE, de.mode[t]);
    en_voicing_set(de.voi[t]);
    en_sel = was;
}
static void de_load(void)                         /* the last save, to the engine (nothing saved, or another format's: the
                                                  * defaults) */
{
    const int n = plat_store_load(OBJ_DE, &de_sv, sizeof de_sv);
    int i, j, t;
    if (n == (int)sizeof de_sv && de_sv.magic == DE_SAVE_MAGIC && de_sv.ver == DE_SAVE_VER && de_sv.nsyn <= SY_N) {
        de.tempo = (uint8_t)(de_sv.tempo < EN_TEMPO_MIN ? EN_TEMPO_MIN : de_sv.tempo > EN_TEMPO_MAX ? EN_TEMPO_MAX : de_sv.tempo);
        en_metro(de_sv.metro_on, de_sv.metro_lvl);
        for (i = 0; i < 4; i++)
            de.rnd[i] = (uint8_t)(de_sv.rnd[i] <= 100 ? de_sv.rnd[i] : 50);
        for (t = 0; t < EN_NTRK; t++) {
            const de_trk_save_t *k = &de_sv.trk[t];
            de.root[t] = (uint8_t)(k->root % 12u);
            de.mode[t] = (uint8_t)(k->mode < EN_NMODES ? k->mode : MODE_AEOLIAN);
            de.voi[t] = (uint8_t)(k->voicing < 3 ? k->voicing : EN_VO_FIRST);
            en_sel = (uint8_t)t;
            en_seq_length(k->len);
            en_seq_division(k->div);
            for (i = 0; i < EN_SEQ_MAX; i++) {
                en_step_t st;
                st.on = k->step[i][0], st.deg = k->step[i][1], st.oct = k->step[i][2], st.voi = k->step[i][3];
                st.len = k->step[i][4];
                if (st.on || i < k->len)
                    en_seq_step(i, &st);
                for (j = 0; j < EN_NLK; j++)
                    if (st.on && k->step[i][5 + j])
                        en_seq_lock(i, j, k->step[i][5 + j] - 1);
            }
            for (i = 0; i < de_sv.nsyn && t < EN_NCHORD; i++)
                en_syn_set(i, k->syn[i]);
            for (i = 0; i < MX_N && t < EN_NCHORD; i++)
                en_mix_set(i, k->mx[i]);
            for (i = 0; i < VP_N && t >= EN_NCHORD; i++)
                en_vp_set(i, k->vp[i]);
        }
        en_sel = (uint8_t)(de_sv.sel < EN_NTRK ? de_sv.sel : 0);
    }
    en_set(P_TEMPO, de.tempo);
    for (t = 0; t < EN_NTRK; t++)
        de_key_send(t);
}

static void de_slots_load(void);
static void de_roll_pattern(void);
static void de_pat_undo(void);
static void de_roll_sound(int full);
static void de_roll_fx(void);
static void de_sound_undo(void);
static void de_init(void)
{
    int i;
    memset(&de, 0, sizeof de);
    de.page = DE_PLAY;
    de.touch = -1;
    de.oct = 1;
    de.step = -1;
    de.tempo = 122;
    de.slot_key = -1;
    de.rnd[0] = 40, de.rnd[1] = 40, de.rnd[2] = 30, de.rnd[3] = 70;
    for (i = 0; i < EN_NTRK; i++)
        de.root[i] = 5, de.mode[i] = MODE_DORIAN, de.voi[i] = EN_VO_FIRST;   /* (F Dorian, first inversions) */
    de.last.on = 1, de.last.deg = 0, de.last.oct = 2, de.last.voi = EN_VO_KEY, de.last.len = 1;
    en_sel = 0;
    en_metro(0, 60);
    de_slots_load();
    de_load();
    de_saved_sum = 0;                            /* (compared once the engine has it all: de_autosave) */
}
static const char *const DE_TRK_SAY[EN_NTRK] = {"TRACK A", "TRACK B", "BASS", "PING"};
static const char *const DE_TRK_TAG[EN_NTRK] = {"A", "B", "BS", "PG"};
static int de_voice_trk(void) { return en_sel >= EN_NCHORD; }
static void de_track(int t)                      /* the other track: the keys held let go first (theirs, the old one's) */
{
    int i;
    for (i = 0; i < NKEYS; i++)
        if (de.note[i]) {
            en_key_chord(de.note[i], 0);
            de.note[i] = 0;
        }
    de.step = -1;
    de.spage = 0;
    en_sel = (uint8_t)(t % EN_NTRK);
    de_say(DE_TRK_SAY[en_sel % EN_NTRK]);
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
        if (de.page == DE_PLAY)                  /* (again: the other track) */
            de_track(en_sel + 1);
        de.page = DE_PLAY;
        break;
    case B_ENV: {                                /* Osc, Filter, Amp env, Filter env, Env 1, Env 2, in turn */
        int i, at = -1;
        if (de_voice_trk()) {                    /* (the bass, the ping: Voice, Shape, Tone; the ping's Delay) */
            const int n = en_sel == EN_T_PING ? 4 : 3;
            de.page = (uint8_t)(de.page >= DE_V1 && de.page < DE_V1 + n - 1 ? de.page + 1 : DE_V1);
            break;
        }
        for (i = 0; i < 6; i++)
            if (DE_ENV_CYCLE[i] == de.page)
                at = i;
        de.page = DE_ENV_CYCLE[at < 0 ? 0 : (at + 1) % 6];
        break;
    }
    case B_LFO:
        if (de_voice_trk()) {
            de_say("CHORD TRACKS ONLY");
            break;
        }
        de.page = de.page == DE_L1 ? DE_L2 : DE_L1;
        break;
    case B_EDIT:                                 /* (a tap) the Random page; there: a new pattern */
        if (de.page == DE_RANDOM)
            de_roll_pattern();
        de.page = DE_RANDOM;
        break;
    case B_SAVE: de_roll_sound(0); break;        /* (a tap) the sound nudged */
    case B_ARP: de_roll_fx(); break;             /* (a tap) the track's delay and its send, new */
    case B_GLO:                                  /* (a tap) Master, track A's EQ, track B's EQ, in turn */
        if (de.page == DE_MASTER) {
            de_track(0);
            de.page = DE_EQ;
        } else if (de.page == DE_EQ && en_sel == 0) {
            de_track(1);
        } else {
            de.page = DE_MASTER;
        }
        break;
    case B_FX:                                   /* Delay, Delay 2, Reverb, Reverb 2, in turn */
        if (de_voice_trk()) {
            if (en_sel == EN_T_PING)
                de.page = DE_V4;
            else
                de_say("NO DELAY ON BASS");
            break;
        }
        de.page = de.page == DE_DELAY ? DE_DELAY2 : de.page == DE_DELAY2 ? DE_REVERB : de.page == DE_REVERB ? DE_REVERB2 : DE_DELAY;
        break;
    case B_SEQ:
        de.page = DE_PATTERN;
        break;
    case B_OCTDN:
    case B_OCTUP: {
        const int up = b == B_OCTUP;
        if (de.edit) {
            const int n = de_spages();
            de_spage_set((de.spage + (up ? 1 : n - 1)) % n);
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
    if (b == B_SEQ && ms >= 1000u && ms < DE_HOLD_MS && !(de.cleared_t && plat_ms() - de.cleared_t < DE_UNDO_MS)) {
        static uint32_t shown;
        const uint32_t left = (DE_HOLD_MS - ms + 749u) / 750u;
        if (left != shown || ms < 1030u) {
            shown = left;
            de_say(left > 1u ? "HOLD TO CLEAR  2" : "HOLD TO CLEAR  1");
        }
    } else if (b == B_SEQ && ms >= DE_HOLD_MS) {
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
    } else if (b == B_EDIT && ms >= DE_SAVE_MS) {
        de.held_done[b] = 1;
        de_pat_undo();
    } else if (b == B_SAVE && ms >= DE_SAVE_MS) {
        de.held_done[b] = 1;
        de_roll_sound(1);
    } else if (b == B_ARP && ms >= DE_SAVE_MS) {
        de.held_done[b] = 1;
        de_sound_undo();
    } else if (b == B_GLO && ms >= DE_SAVE_MS) {
        de.held_done[b] = 1;
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
static void de_step_set(int i, const en_step_t *t)   /* a step, to the engine; and here at once (two knobs turned in one
                                                  * frame: the second reads the first's) */
{
    en_seq_step(i, t);
    en_seq[i].on = t->on, en_seq[i].deg = t->deg, en_seq[i].oct = t->oct, en_seq[i].voi = t->voi, en_seq[i].len = t->len;
    if (!t->on)                                  /* (emptied: its locks gone, as the engine's) */
        en_seq[i].lock[0] = en_seq[i].lock[1] = en_seq[i].lock[2] = 0;
}
static void de_step_get(int i, en_step_t *t)
{
    t->on = en_seq[i].on, t->deg = en_seq[i].deg, t->oct = en_seq[i].oct, t->voi = en_seq[i].voi, t->len = en_seq[i].len;
    t->lock[0] = en_seq[i].lock[0], t->lock[1] = en_seq[i].lock[1], t->lock[2] = en_seq[i].lock[2];
}
/* ------------------------------------------------- slots, randomise --- */
/* Eight pattern slots and eight sound slots, either track's to take or fill, kept in flash (OBJ_SLOTS). SEQ held, one of
 * the first eight white keys: tapped, the slot's pattern to the selected track; held, the track's pattern to the slot.
 * ENV held: the same with the sound (the synth, the delay, the plate's send, the EQ; not the level, not the plate). */
#define DE_NSLOT 8
#define DE_SLOT_MAGIC 0x544C5344u                /* "DSLT" */
#define DE_SLOT_VER 2u
#define DE_SLOT_HOLD 800u                        /* a slot's key held: saved */
typedef struct {
    uint8_t used, len, div, rsv;
    uint8_t step[EN_SEQ_MAX][5];                 /* on | degree << 1 | octave << 4 | voicing << 6; length; its three locks */
} de_pslot_t;
typedef struct {
    uint8_t used, rsv[3];
    int8_t syn[SY_N], mx[MX_N], vp[VP_N];        /* (used: 1 a chord track's sound, 2 the bass's or the ping's) */
} de_sslot_t;
typedef struct {
    uint32_t magic;
    uint16_t ver, nsyn;
    de_pslot_t pat[DE_NSLOT];
    de_sslot_t snd[DE_NSLOT];
} de_slots_t;
_Static_assert(sizeof(de_slots_t) <= PLAT_STORE_MAX, "the slots fit their flash object");
static de_slots_t de_slots;
/* (one step back: the pattern before the last roll or slot taken, the sound before the last roll, nudge or slot) */
static struct {
    uint8_t ok, trk, len, div;
    en_step_t st[EN_SEQ_MAX];
} de_pundo;
static struct {
    uint8_t ok, trk;
    int16_t syn[SY_N], mx[MX_N], vp[VP_N];
} de_sundo;
static uint32_t de_rng;

static void de_slots_load(void)
{
    const int n = plat_store_load(OBJ_SLOTS, &de_slots, sizeof de_slots);
    if (n != (int)sizeof de_slots || de_slots.magic != DE_SLOT_MAGIC || de_slots.ver != DE_SLOT_VER || de_slots.nsyn != SY_N) {
        memset(&de_slots, 0, sizeof de_slots);   /* (nothing kept, or another firmware's: empty) */
        de_slots.magic = DE_SLOT_MAGIC, de_slots.ver = DE_SLOT_VER, de_slots.nsyn = SY_N;
    }
    de_pundo.ok = de_sundo.ok = 0;
}
static int de_syn_shared(int p) { return p >= SY_RDECAY && p <= SY_RFREEZE && p != SY_RMIX; }   /* the plate's own */
static void de_pat_keep(void)                    /* the selected track's pattern, kept for the undo */
{
    int i;
    de_pundo.ok = 1, de_pundo.trk = en_sel, de_pundo.len = en_seq_len, de_pundo.div = en_seq_div;
    for (i = 0; i < EN_SEQ_MAX; i++)
        de_step_get(i, &de_pundo.st[i]);
}
static void de_pat_put(int len, int div, const en_step_t *st)   /* a whole pattern to the selected track */
{
    int i, j;
    len = len < 1 ? 1 : len > EN_SEQ_MAX ? EN_SEQ_MAX : len;
    en_seq_length(len);
    en_seq_len = (uint8_t)len;
    en_seq_division(div);
    en_seq_div = (uint8_t)(div < EN_NDIV ? div : EN_DIV_16);
    for (i = 0; i < EN_SEQ_MAX; i++) {
        en_step_t t, was;
        de_step_get(i, &was);
        t = st[i];
        if (!t.on && !was.on)
            continue;
        t.len = (uint8_t)(t.len < 1 ? 1 : t.len);
        de_step_set(i, &t);
        for (j = 0; j < EN_NLK; j++)
            if (t.on && (t.lock[j] || was.lock[j])) {
                en_seq_lock(i, j, (int)t.lock[j] - 1);
                en_seq[i].lock[j] = t.lock[j];
            }
    }
    if (de.spage >= de_spages())
        de.spage = 0;
}
static void de_pat_undo(void)
{
    static en_step_t now_st[EN_SEQ_MAX];
    int i, len, div;
    if (!de_pundo.ok || de_pundo.trk != en_sel) {
        de_say("NOTHING TO UNDO");
        return;
    }
    len = en_seq_len, div = en_seq_div;          /* (the undo undone: the two swapped) */
    for (i = 0; i < EN_SEQ_MAX; i++)
        de_step_get(i, &now_st[i]);
    de_pat_put(de_pundo.len, de_pundo.div, de_pundo.st);
    de_pundo.len = (uint8_t)len, de_pundo.div = (uint8_t)div;
    for (i = 0; i < EN_SEQ_MAX; i++)
        de_pundo.st[i] = now_st[i];
    de_say("PATTERN BACK");
}
static void de_snd_keep(void)
{
    int i;
    de_sundo.ok = 1, de_sundo.trk = en_sel;
    for (i = 0; i < SY_N; i++)
        de_sundo.syn[i] = en_syn[i];
    for (i = 0; i < MX_N; i++)
        de_sundo.mx[i] = en_mx[i];
    for (i = 0; i < VP_N; i++)
        de_sundo.vp[i] = en_vp[i];
}
static void de_syn_put(int p, int v)             /* a synth parameter, here and to the engine (clamped to its range) */
{
    v = v < SYN_INFO[p].lo ? SYN_INFO[p].lo : v > SYN_INFO[p].hi ? SYN_INFO[p].hi : v;
    if (en_syn[p] == v)
        return;
    en_syn_set(p, v);
    en_syn[p] = (int16_t)v;
}
static void de_mx_put(int p, int v)
{
    if (en_mx[p] == v)
        return;
    en_mix_set(p, v);
    en_mx[p] = (int16_t)v;
}
static void de_vp_put(int p, int v)
{
    v = v < VP_INFO[p].lo ? VP_INFO[p].lo : v > VP_INFO[p].hi ? VP_INFO[p].hi : v;
    if (en_vp[p] == v)
        return;
    en_vp_set(p, v);
    en_vp[p] = (int16_t)v;
}
static void de_sound_undo(void)
{
    int i;
    if (!de_sundo.ok || de_sundo.trk != en_sel) {
        de_say("NOTHING TO UNDO");
        return;
    }
    if (de_voice_trk()) {
        for (i = 0; i < VP_N; i++) {
            const int was = en_vp[i];
            de_vp_put(i, de_sundo.vp[i]);
            de_sundo.vp[i] = (int16_t)was;
        }
        de_say("SOUND BACK");
        return;
    }
    for (i = 0; i < SY_N; i++) {
        const int was = en_syn[i];
        if (de_syn_shared(i))
            continue;
        de_syn_put(i, de_sundo.syn[i]);
        de_sundo.syn[i] = (int16_t)was;
    }
    for (i = MX_EQ; i < MX_N; i++) {
        const int was = en_mx[i];
        de_mx_put(i, de_sundo.mx[i]);
        de_sundo.mx[i] = (int16_t)was;
    }
    de_say("SOUND BACK");
}
static void de_slot_msg(const char *a, int snd, int k)   /* "LOADED P3", "SAVED S1", "P4 EMPTY" */
{
    char b[24];
    b[0] = 0;
    de_cat(b, a);
    de_cat(b, snd ? "S" : "P");
    de_num(b + de_len(b), k + 1);
    de_say(b);
}
static void de_slot_save(int snd, int k)
{
    int i;
    if (snd) {
        de_sslot_t *q = &de_slots.snd[k];
        q->used = (uint8_t)(de_voice_trk() ? 2 : 1);
        for (i = 0; i < VP_N; i++)
            q->vp[i] = (int8_t)en_vp[i];
        for (i = 0; i < SY_N; i++)
            q->syn[i] = (int8_t)en_syn[i];
        for (i = 0; i < MX_N; i++)
            q->mx[i] = (int8_t)en_mx[i];
    } else {
        de_pslot_t *q = &de_slots.pat[k];
        q->used = 1, q->len = en_seq_len, q->div = en_seq_div;
        for (i = 0; i < EN_SEQ_MAX; i++) {
            en_step_t t;
            de_step_get(i, &t);
            q->step[i][0] = (uint8_t)((t.on != 0) | (t.deg & 7) << 1 | (t.oct & 3) << 4 | (t.voi & 3) << 6);
            q->step[i][1] = t.len, q->step[i][2] = t.lock[0], q->step[i][3] = t.lock[1], q->step[i][4] = t.lock[2];
        }
    }
    if (plat_store_save(OBJ_SLOTS, &de_slots, sizeof de_slots) == 0)
        de_slot_msg("SAVED ", snd, k);
    else
        de_say("SAVE FAILED");
}
static void de_slot_take(int snd, int k)
{
    static en_step_t st[EN_SEQ_MAX];
    int i;
    if (snd ? !de_slots.snd[k].used : !de_slots.pat[k].used) {
        de_slot_msg("EMPTY ", snd, k);
        return;
    }
    if (snd) {
        const de_sslot_t *q = &de_slots.snd[k];
        if ((q->used == 2) != de_voice_trk()) {  /* (a chord's sound and a voice's: not each other's) */
            de_say(q->used == 2 ? "A BASS OR PING SOUND" : "A CHORD SOUND");
            return;
        }
        de_snd_keep();
        if (de_voice_trk()) {
            for (i = 0; i < VP_N; i++)
                if (i != VP_LEVEL)
                    de_vp_put(i, q->vp[i]);
            de_slot_msg("LOADED ", snd, k);
            return;
        }
        for (i = 0; i < SY_N; i++)
            if (!de_syn_shared(i))
                de_syn_put(i, q->syn[i]);
        for (i = MX_EQ; i < MX_N; i++)
            de_mx_put(i, q->mx[i]);
    } else {
        const de_pslot_t *q = &de_slots.pat[k];
        de_pat_keep();
        for (i = 0; i < EN_SEQ_MAX; i++) {
            const uint8_t b = q->step[i][0];
            st[i].on = b & 1u, st[i].deg = (b >> 1) & 7u, st[i].oct = (b >> 4) & 3u, st[i].voi = (b >> 6) & 3u;
            st[i].len = q->step[i][1], st[i].lock[0] = q->step[i][2], st[i].lock[1] = q->step[i][3], st[i].lock[2] = q->step[i][4];
        }
        de_pat_put(q->len, q->div, st);
    }
    de_slot_msg("LOADED ", snd, k);
}
static int de_slot_key(int key, int down)        /* SEQ or ENV held: a key is a slot's (1: taken here) */
{
    const int snd = (de.btn >> B_ENV & 1u) && !(de.btn >> B_SEQ & 1u);
    int i, k = -1;
    if (!(de.btn & (1u << B_SEQ | 1u << B_ENV))) {
        if (!down && de.slot_key >= 0 && DE_WHITE[de.slot_key] == key) {   /* (the button let go first) */
            de.slot_key = -1;
            return 1;
        }
        return 0;
    }
    for (i = 0; i < DE_NSLOT; i++)
        if (DE_WHITE[i] == key)
            k = i;
    de.held_done[B_SEQ] = de.held_done[B_ENV] = 1;   /* (the button a modifier now: no page, no clear) */
    if (k < 0)
        return 1;
    if (down) {
        de.slot_key = (int8_t)k, de.slot_snd = (uint8_t)snd, de.slot_done = 0, de.slot_t = plat_ms();
    } else if (de.slot_key == k) {
        if (!de.slot_done)
            de_slot_take(de.slot_snd, k);
        de.slot_key = -1;
    }
    return 1;
}
static void de_slot_tick(uint32_t now)           /* a slot's key held: saved; SEQ or ENV held a moment: what the keys do */
{
    const uint32_t mod = de.btn & (1u << B_SEQ | 1u << B_ENV);
    if (de.slot_key >= 0 && !de.slot_done && mod && now - de.slot_t >= DE_SLOT_HOLD) {
        de.slot_done = 1;
        de_slot_save(de.slot_snd, de.slot_key);
    }
    if (!mod) {
        de.slot_hint = 0;
    } else if (!de.slot_hint && de.slot_key < 0 &&
               now - de.down_t[de.btn >> B_SEQ & 1u ? B_SEQ : B_ENV] >= 350u && !de.held_done[B_SEQ] && !de.held_done[B_ENV]) {
        de.slot_hint = 1;
        de_say(de.btn >> B_SEQ & 1u ? "KEYS 1-8 PATTERN SLOTS" : "KEYS 1-8 SOUND SLOTS");
    }
}

/* Randomise, once, by hand: nothing moves by itself. Each roll can be undone (one step back).
 *   the pattern (EDIT on the Random page): the selected track's steps afresh, in its key. Density: how many chords;
 *     Variation: how many different chords of the scale; Length: how long they are held; Offbeat: off the beat, as
 *     dub chords sit, or on it.
 *   the sound (SAVE): a tap nudges every knob of the synth a little; held, a new sound, within ranges that suit dub
 *     chords.
 *   the effects (ARP): the track's delay and its send to the plate, new. The plate is both tracks': left as it is. */
static uint32_t de_rand(void)
{
    if (!de_rng)
        de_rng = 0x9E3779B9u;
    de_rng ^= plat_ms() * 2654435761u;           /* (when it's asked: no two rolls alike) */
    de_rng ^= de_rng << 13;
    de_rng ^= de_rng >> 17;
    de_rng ^= de_rng << 5;
    return de_rng;
}
static int de_ri(int lo, int hi) { return hi <= lo ? lo : lo + (int)((de_rand() >> 8) % (uint32_t)(hi - lo + 1)); }   /* lo..hi */
static void de_roll_pattern(void)
{
    static en_step_t st[EN_SEQ_MAX];
    static const uint8_t ORDER[7] = {0, 3, 4, 5, 2, 6, 1};   /* the degrees, the likeliest first: i, iv, v, vi, iii, vii, ii */
    const int n = de_nsteps(), m = de_count(de_pool()) > 0 ? de_count(de_pool()) : 1;
    const int dens = de.rnd[0], vary = de.rnd[1], lng = de.rnd[2], off = de.rnd[3];
    int count = 1 + dens * (n / 2 - 1) / 100, kinds = 1 + vary * (m - 1) / 100, i, c;
    uint8_t deg[7];
    count = count < 1 ? 1 : count > n ? n : count;
    kinds = kinds > 7 ? 7 : kinds;
    for (i = 0, c = 0; i < 7 && c < kinds; i++)  /* (the scale's own degrees only) */
        if (ORDER[i] < m)
            deg[c++] = ORDER[i];
    kinds = c ? c : 1;
    if (!c)
        deg[0] = 0;
    de_pat_keep();
    memset(st, 0, sizeof st);
    for (c = 0; c < count; c++) {                /* a step for each chord: drawn by weight, each step once */
        int total = 0, pick;
        for (i = 0; i < n; i++)
            if (!st[i].on)
                total += (i & 3) == 0 ? 105 - off : (i & 3) == 2 ? 15 + off : 5 + off / 2;
        if (total <= 0)
            break;
        pick = de_ri(0, total - 1);
        for (i = 0; i < n; i++) {
            if (st[i].on)
                continue;
            pick -= (i & 3) == 0 ? 105 - off : (i & 3) == 2 ? 15 + off : 5 + off / 2;
            if (pick < 0)
                break;
        }
        if (i >= n)
            break;
        st[i].on = 1;
        st[i].deg = deg[de_ri(0, kinds) % kinds];   /* (the first, the root, twice as likely) */
        st[i].oct = (uint8_t)(en_sel == EN_T_PING ? de_ri(2, 3) : 2), st[i].voi = EN_VO_KEY, st[i].len = 1;
    }
    for (i = 0; i < n; i++)                      /* each held up to the next, as far as Length lets it */
        if (st[i].on) {
            int gap = 1;
            while (gap < n && !st[(i + gap) % n].on)
                gap++;
            st[i].len = (uint8_t)(1 + de_ri(0, (gap - 1) * lng / 100));
        }
    de_pat_put(n, en_seq_div, st);
    de_say("NEW PATTERN");
}
/* the synth's knobs and the range a new sound draws each from */
static const struct {
    uint8_t id;
    int8_t lo, hi;
} DE_RSND[] = {
    {SY_FOLD, 0, 30}, {SY_DETUNE, 8, 40}, {SY_LEVEL, 62, 78}, {SY_CUT, 26, 58}, {SY_RESO, 15, 60}, {SY_FENV, 10, 70},
    {SY_KTRK, 20, 70}, {SY_AA, 0, 14}, {SY_AD, 30, 60}, {SY_AS, 0, 50}, {SY_AR, 25, 60}, {SY_FA, 0, 10}, {SY_FD, 20, 55},
    {SY_FS, 0, 30}, {SY_FR, 20, 50}, {SY_1A, 0, 40}, {SY_1D, 30, 70}, {SY_1S, 0, 40}, {SY_1R, 30, 60}, {SY_2A, 0, 40},
    {SY_2D, 30, 70}, {SY_2S, 0, 40}, {SY_2R, 30, 60}, {SY_L1RATE, 10, 55}, {SY_L2RATE, 8, 45},
};
#define DE_NRSND ((int)(sizeof DE_RSND / sizeof DE_RSND[0]))
static void de_roll_voice(int full)              /* the bass's or the ping's sound: nudged, or new within what suits it */
{
    static const uint8_t NUDGE[7] = {VP_FM, VP_FB, VP_ATT, VP_DEC, VP_MDEC, VP_CUT, VP_DRIVE};
    static const uint8_t PV[4] = {3, 5, 6, 7}, BR[4] = {0, 1, 1, 3};
    const int ping = en_sel == EN_T_PING;
    int i;
    if (!full) {
        for (i = 0; i < 7; i++)
            de_vp_put(NUDGE[i], en_vp[NUDGE[i]] + de_ri(-5, 5));
        de_say("SOUND NUDGED");
        return;
    }
    de_vp_put(VP_VOICE, ping ? PV[de_ri(0, 3)] : de_ri(0, 4));
    de_vp_put(VP_FM, de_ri(30, 80));
    de_vp_put(VP_RATIO, ping ? de_ri(1, 7) : BR[de_ri(0, 3)]);
    de_vp_put(VP_FB, de_ri(0, 2) ? 0 : de_ri(5, 40));
    de_vp_put(VP_ATT, de_ri(0, ping ? 5 : 10));
    de_vp_put(VP_DEC, ping ? de_ri(25, 65) : de_ri(0, 1) ? 100 : de_ri(55, 85));
    de_vp_put(VP_MDEC, de_ri(30, 70));
    de_vp_put(VP_GLIDE, ping ? de_ri(30, 90) : de_ri(0, 2) ? 0 : de_ri(15, 45));
    de_vp_put(VP_CUT, ping ? de_ri(70, 100) : de_ri(55, 100));
    de_vp_put(VP_DRIVE, de_ri(0, 1) ? 0 : de_ri(5, 40));
    de_vp_put(VP_REV, ping ? de_ri(20, 55) : de_ri(0, 8));
    de_say("NEW SOUND");
}
static void de_roll_sound(int full)
{
    static const uint8_t WAVE[20] = {EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SAW,
                                     EN_WAVE_SAW, EN_WAVE_SAW, EN_WAVE_SQUARE, EN_WAVE_SQUARE, EN_WAVE_SQUARE, EN_WAVE_SQUARE,
                                     EN_WAVE_SQUARE, EN_WAVE_SQUARE, EN_WAVE_SQUARE, EN_WAVE_TRI, EN_WAVE_TRI, EN_WAVE_TRI,
                                     EN_WAVE_SINE};
    static const uint8_t DST[6] = {EN_MD_CUTOFF, EN_MD_CUTOFF, EN_MD_RESO, EN_MD_FOLD, EN_MD_DETUNE, EN_MD_ECHO};
    int i;
    de_snd_keep();
    if (de_voice_trk()) {
        de_roll_voice(full);
        return;
    }
    if (!full) {                                 /* a nudge: each knob a few steps, either way */
        for (i = 0; i < DE_NRSND; i++)
            de_syn_put(DE_RSND[i].id, en_syn[DE_RSND[i].id] + de_ri(-6, 6));
        for (i = 0; i < 4; i++) {                /* (the depths that are set, a little) */
            static const uint8_t AMT[4] = {SY_1AMT, SY_2AMT, SY_L1AMT, SY_L2AMT};
            if (en_syn[AMT[i]])
                de_syn_put(AMT[i], en_syn[AMT[i]] + de_ri(-6, 6));
        }
        de_say("SOUND NUDGED");
        return;
    }
    de_syn_put(SY_WAVE, WAVE[de_ri(0, 19)]);
    for (i = 0; i < DE_NRSND; i++)
        de_syn_put(DE_RSND[i].id, de_ri(DE_RSND[i].lo, DE_RSND[i].hi));
    if (de_ri(0, 2))                             /* (two in three: no fold at all) */
        de_syn_put(SY_FOLD, 0);
    for (i = 0; i < 4; i++) {                    /* Env 1, Env 2, the LFOs: each one in two to a target, a depth */
        static const uint8_t D[4] = {SY_1DST, SY_2DST, SY_L1DST, SY_L2DST}, A[4] = {SY_1AMT, SY_2AMT, SY_L1AMT, SY_L2AMT};
        const int on = de_ri(0, 1);
        de_syn_put(D[i], on ? DST[de_ri(0, 5)] : EN_MD_NONE);
        de_syn_put(A[i], on ? (de_ri(0, 3) ? 1 : -1) * de_ri(10, 45) : 0);
    }
    de_syn_put(SY_L1SHAPE, de_ri(0, 3) ? de_ri(EN_LS_SINE, EN_LS_TRI) : de_ri(EN_LS_SAW, EN_LS_RANDOM));
    de_syn_put(SY_L2SHAPE, de_ri(0, 3) ? de_ri(EN_LS_SINE, EN_LS_TRI) : de_ri(EN_LS_SAW, EN_LS_RANDOM));
    de_syn_put(SY_L1SYNC, de_ri(0, 2) ? 0 : de_ri(1, 5));
    de_syn_put(SY_L2SYNC, de_ri(0, 2) ? 0 : de_ri(1, 4));
    de_say("NEW SOUND");
}
static void de_roll_fx(void)
{
    static const uint8_t TIME[8] = {3, 3, 3, 5, 5, 2, 4, 6};   /* 1/8. (the dub one) the likeliest, 1/4, 1/8, 1/4T, 1/4. */
    if (en_sel == EN_T_BASS) {
        de_say("NO DELAY ON BASS");
        return;
    }
    de_snd_keep();
    if (en_sel == EN_T_PING) {
        de_vp_put(VP_DTIME, TIME[de_ri(0, 7)]);
        de_vp_put(VP_DFB, de_ri(30, 75));
        de_vp_put(VP_DMIX, de_ri(25, 60));
        de_vp_put(VP_DTONE, de_ri(30, 70));
        de_vp_put(VP_REV, de_ri(20, 55));
        de_say("NEW DELAY");
        return;
    }
    de_syn_put(SY_DSYNC, 1);
    de_syn_put(SY_DTIME, TIME[de_ri(0, 7)]);
    de_syn_put(SY_DFB, de_ri(35, 85));
    de_syn_put(SY_DMIX, de_ri(20, 55));
    de_syn_put(SY_DTONE, de_ri(25, 70));
    de_syn_put(SY_DWOW, de_ri(5, 45));
    de_syn_put(SY_DDRIVE, de_ri(0, 45));
    de_syn_put(SY_DWIDTH, de_ri(30, 90));
    de_syn_put(SY_DBLUR, de_ri(30, 80));
    de_syn_put(SY_DPING, de_ri(0, 2) == 0);
    de_syn_put(SY_RMIX, de_ri(15, 50));
    de_say("NEW DELAY");
}

static void de_key(int key, int down)
{
    if (de_slot_key(key, down))                  /* (SEQ or ENV held: a slot's key) */
        return;
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
                de_step_set(st, &t);
            }
            de.note[key] = (uint8_t)en_seq_note(&t);   /* (heard while held) */
            if (!en_seq_playing)
                en_key_chord(de.note[key], 1);
        } else if (de.step == st) {
            if (de.step_was_on && !de.step_turned) {   /* a tap on a step: off */
                de_step_get(st, &t);
                t.on = 0;
                de_step_set(st, &t);
            }
            if (!en_seq_playing)
                en_key_chord(de.note[key], 0);
            de.step = -1;
        }
        return;
    }
    if (down) {                                  /* playing: a key in the scale, its chord */
        const int note = 53 + key + 12 * ((int)de.oct - 1);
        if (!(de_pool_abs() >> ((5u + (uint32_t)key) % 12u) & 1u))
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
    if (id >= DE_VP)
        return en_vp[(id - DE_VP) % VP_N];
    if (id >= DE_ID_LVL && id < DE_ID_RND && id - DE_ID_LVL >= EN_NCHORD)
        return en_trk[(id - DE_ID_LVL) % EN_NTRK].vp[VP_LEVEL];
    return id >= DE_ID_RND ? de.rnd[(id - DE_ID_RND) & 3] : id >= DE_ID_LVL ? en_trk[(id - DE_ID_LVL) % EN_NTRK].mx[MX_LEVEL] : id == DE_ID_BAND ? de_band()
           : id >= DE_MX ? en_mx[id - DE_MX] : id >= 0 ? en_syn[id] : 0;
}
static void de_param_range(int id, int *lo, int *hi)
{
    if (id >= DE_VP)
        *lo = VP_INFO[(id - DE_VP) % VP_N].lo, *hi = VP_INFO[(id - DE_VP) % VP_N].hi;
    else if (id >= DE_ID_LVL)
        *lo = 0, *hi = 100;
    else if (id == DE_ID_BAND)
        *lo = 0, *hi = EN_NEQ - 1;
    else if (id >= DE_MX)
        *lo = en_mix_lo(id - DE_MX), *hi = en_mix_hi(id - DE_MX);
    else
        *lo = SYN_INFO[id].lo, *hi = SYN_INFO[id].hi;
}
static void de_param_turn(int id, int d)         /* turned d detents: a click a step (a wide range: two) */
{
    int lo, hi, v;
    if (id < 0)
        return;
    de_param_range(id, &lo, &hi);
    v = de_param_get(id) + d * (hi - lo > 40 ? 2 : 1);
    v = v < lo ? lo : v > hi ? hi : v;
    if (id >= DE_VP) {
        en_vp_set(id - DE_VP, v);
        en_vp[(id - DE_VP) % VP_N] = (int16_t)v;
    } else if (id >= DE_ID_RND) {
        de.rnd[(id - DE_ID_RND) & 3] = (uint8_t)v;
    } else if (id >= DE_ID_LVL) {                /* a track's level, whichever is selected */
        const uint8_t was = en_sel, t = (uint8_t)((id - DE_ID_LVL) % EN_NTRK);
        en_sel = t;
        if (t >= EN_NCHORD) {
            en_vp_set(VP_LEVEL, v);
            en_trk[t].vp[VP_LEVEL] = (int16_t)v;
        } else {
            en_mix_set(MX_LEVEL, v);
            en_trk[t].mx[MX_LEVEL] = (int16_t)v;
        }
        en_sel = was;
    } else if (id == DE_ID_BAND) {
        de.band = (uint8_t)v;
    } else if (id >= DE_MX) {
        en_mix_set(id - DE_MX, v);
        en_mx[id - DE_MX] = (int16_t)v;
    } else {
        en_syn_set(id, v);
        en_syn[id] = (int16_t)v;                 /* (shown at once: the engine sets the same) */
    }
}
static const char *de_param_name(int id)
{
    static const char *const F[EQ_NF] = {"Freq", "Gain", "Q"};
    static const char *const R[4] = {"Density", "Variation", "Length", "Offbeat"};
    static const char *const L[EN_NTRK] = {"Level A", "Level B", "Bass", "Ping"};
    if (id >= DE_VP)
        return id - DE_VP == VP_GLIDE && en_sel == EN_T_PING ? "Spread" : VP_INFO[(id - DE_VP) % VP_N].name;
    return id >= DE_ID_RND ? R[(id - DE_ID_RND) & 3] : id >= DE_ID_LVL ? L[(id - DE_ID_LVL) % EN_NTRK] : id == DE_ID_BAND ? "Band"
           : id >= DE_MX ? (id - DE_MX == MX_LEVEL ? "Level" : F[(id - DE_MX - MX_EQ) % EQ_NF]) : id >= 0 ? SYN_INFO[id].name : "";
}
static void de_hz(char *b, float hz)             /* "98", "2.0K", "12K" */
{
    if (hz < 1000.0f) {
        de_num(b, (int)(hz + 0.5f));
    } else if (hz < 10000.0f) {
        const int d = (int)(hz * 0.01f + 0.5f);
        de_num(b, d / 10);
        de_cat(b, ".");
        de_num(b + de_len(b), d % 10);
        de_cat(b, "K");
    } else {
        de_num(b, (int)(hz * 0.001f + 0.5f));
        de_cat(b, "K");
    }
}
static void de_param_text(int id, char *b)        /* its value as shown */
{
    static const char *const BAND[EN_NEQ] = {"Lo cut", "Lo shelf", "Bell 1", "Bell 2", "Bell 3", "Bell 4", "Hi shelf", "Hi cut"};
    const int v = de_param_get(id);
    b[0] = 0;
    if (id < 0)
        return;
    if (id >= DE_VP) {
        const int q = id - DE_VP;
        if (q == VP_VOICE)
            de_cat(b, EN_VOICE_NAME[v % EN_NVOICE]);
        else if (q == VP_RATIO)
            de_cat(b, EN_RATIO_NAME[v % EN_NRATIO]);
        else if (q == VP_DTIME)
            de_cat(b, EN_DTIME_NAME[v % EN_NDTIME]);
        else if (q == VP_DEC && en_sel == EN_T_BASS && v >= 100)
            de_cat(b, "Hold");
        else
            de_num(b, v);
        return;
    }
    if (id == DE_ID_BAND) {
        de_cat(b, BAND[v % EN_NEQ]);
        return;
    }
    if (id >= DE_MX && id < DE_ID_BAND && id - DE_MX >= MX_EQ) {
        const int band = (id - DE_MX - MX_EQ) / EQ_NF, f = (id - DE_MX - MX_EQ) % EQ_NF;
        if (f == EQ_FREQ) {
            if ((band == EN_EQ_LOCUT && v == 0) || (band == EN_EQ_HICUT && v == 100))
                de_cat(b, "Off");
            else
                de_hz(b, en_eq_hz(v));
        } else if (f == EQ_GAIN) {
            if (v > 0)
                de_cat(b, "+");
            de_num(b + de_len(b), v);
            de_cat(b, "dB");
        } else {
            de_num(b, v);
        }
        return;
    }
    if (id >= DE_MX) {
        de_num(b, v);
        return;
    }
    switch (id) {
    case SY_WAVE: de_cat(b, EN_WAVE_NAME[v % EN_NWAVE]); return;
    case SY_DMODE: de_cat(b, EN_DMODE_NAME[v % EN_NDMODE]); return;
    case SY_DPING: de_cat(b, v ? "On" : "Off"); return;
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
        const int m = de_count(de_pool()) > 0 ? de_count(de_pool()) : 1;
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
        de_step_set(de.step, &t);
        de.last = t, de.last.lock[0] = de.last.lock[1] = de.last.lock[2] = 0;
        de.step_turned = 1;
        if (k <= 2 && !en_seq_playing) {         /* (the new chord heard) */
            const int key = DE_WHITE[de.step % 16];
            en_key_chord(de.note[key], 0);
            de.note[key] = (uint8_t)en_seq_note(&t);
            if (t.voi != EN_VO_KEY)
                en_voicing_set(t.voi);
            en_key_chord(de.note[key], 1);
            en_voicing_set(de.voi[en_sel]);
        }
        return;
    }
    if (de_sound_page(de.page)) {
        de_param_turn(de_kid(de.page, k), d);
        return;
    }
    if (de.page == DE_PLAY) {
        if (k == 0) {
            const int v = (int)de.tempo + d;
            de.tempo = (uint8_t)(v < EN_TEMPO_MIN ? EN_TEMPO_MIN : v > EN_TEMPO_MAX ? EN_TEMPO_MAX : v);
            en_set(P_TEMPO, de.tempo);
        } else if (k == 1) {
            de.root[en_sel] = (uint8_t)((((int)de.root[en_sel] + d) % 12 + 12) % 12);
            en_set(P_ROOT, de.root[en_sel]);
        } else if (k == 2) {                     /* the modes on the knob: the seven of the major scale */
            int at = 0, i;
            for (i = 0; i < DE_NMODE; i++)
                if (DE_MODES[i] == de.mode[en_sel])
                    at = i;
            de.mode[en_sel] = DE_MODES[((at + d) % DE_NMODE + DE_NMODE) % DE_NMODE];
            en_set(P_MODE, de.mode[en_sel]);
        } else {
            if (de_voice_trk())                  /* (one note: no voicing) */
                return;
            de.voi[en_sel] = (uint8_t)(((int)de.voi[en_sel] + d % 3 + 3) % 3);
            en_voicing_set(de.voi[en_sel]);
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

static void de_lock_turn(int which, int d)       /* the step held: a lock a step on from where it is (none: from the page's
                                                  * value); turned down past 0: none again */
{
    static const char *const N[EN_NLK] = {"LEVEL ", "CUTOFF ", "DELAY MIX "};
    static const uint8_t SRC[EN_NLK] = {SY_LEVEL, SY_CUT, SY_DMIX}, VSRC[EN_NLK] = {VP_LEVEL, VP_CUT, VP_DMIX};
    char b[24];
    en_step_t t;
    int v;
    de_step_get(de.step, &t);
    if (!t.on)
        return;
    if (en_sel == EN_T_BASS && which == EN_LK_SEND)   /* (the bass: no delay) */
        return;
    v = (t.lock[which] ? t.lock[which] - 1 : de_voice_trk() ? en_vp[VSRC[which]] : en_syn[SRC[which]]) + 2 * d;
    v = v > 100 ? 100 : v;
    en_seq_lock(de.step, which, v < 0 ? -1 : v);
    en_seq[de.step].lock[which] = (uint8_t)(v < 0 ? 0 : v + 1);
    de.step_turned = 1;
    b[0] = 0;
    de_cat(b, N[which]);
    if (v < 0)
        de_cat(b, "FREE");
    else
        de_num(b + de_len(b), v);
    de_say(b);
}
static void midi_in(void)                        /* USB MIDI in: a note in the track's scale, its chord, as the keys play it */
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        const uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if ((st == 0x90u || st == 0x80u) && de_pool_abs() >> (n % 12u) & 1u)
            en_key_chord((int)n, st == 0x90u && v);
    }
}
#define DE_ON_RELEASE (1u << B_SEQ | 1u << B_GLO | 1u << B_ENV | 1u << B_EDIT | 1u << B_SAVE | 1u << B_ARP)
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
                if (!(DE_ON_RELEASE >> i & 1u))   /* (SEQ, GLO, ENV, EDIT, SAVE, ARP act on release, or on a hold) */
                    de_button((int)i);
            } else if ((DE_ON_RELEASE >> i & 1u) && !de.held_done[i]) {
                de_button((int)i);
            }
        } else if (btn & m) {
            de_hold((int)i, now - de.down_t[i]);
        }
    }
    de_slot_tick(now);
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
    {   /* ALGORITHM and PRESETS: on Env 1, Env 2 the target and the depth; on the LFOs the sync; a step held: its
         * Level's and its Cutoff's locks (SELECT: its Delay mix's) */
        const int32_t da = plat_enc(EN_ALGO), dp = plat_enc(EN_PRESET);
        if (de.edit && de.step >= 0) {
            if (da)
                de_lock_turn(EN_LK_LEVEL, (int)da), de.act_t = now;
            if (dp)
                de_lock_turn(EN_LK_CUTOFF, (int)dp), de.act_t = now;
        }
        if (!de_sound_page(de.page) && de.edit && de.step < 0 && da)   /* (Play, Pattern, step edit: the page of steps) */
            de_spage_set(de.spage + (da > 0 ? 1 : -1)), de.act_t = now;
        if (de_sound_page(de.page) && !(de.edit && de.step >= 0)) {
            if (da)
                de_param_turn(de_kid(de.page, 4), (int)da), de.act_t = de.touch_t = now, de.touch = 4;
            if (dp)
                de_param_turn(de_kid(de.page, 5), (int)dp), de.act_t = de.touch_t = now, de.touch = 5;
        }
    }
    {   /* SELECT: on the Delay page, Sync or Free */
        const int32_t ds = plat_enc(EN_SELECT);
        if (ds && de.edit && de.step >= 0)
            de_lock_turn(EN_LK_SEND, (int)ds), de.act_t = now;
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
    w = cv_text(w + 6, 3, &FONT_T_B, DE_TRK_TAG[en_sel % EN_NTRK], DE_OR) + 2;   /* the track */
    b[0] = 0;
    de_cat(b, EN_NOTE_NAME[de.root[en_sel] % 12]);
    de_cat(b, " ");
    de_upper(t, EN_MODE_NAME[de.mode[en_sel] % EN_NMODES], 4);
    de_cat(b, t);
    x = cv_text(w + 6, 4, &FONT_T_S, b, DE_FG);
    /* the transport, at the right: the tempo; the click; record (or edit); play */
    de_num(b, de.tempo);
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
static float de_bar_s(void) { return 240.0f / (float)(de.tempo > 20 ? de.tempo : 120); }   /* a bar, s */
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

static void de_pic_master(void)                  /* the meters: track A, track B, the output; dB, the peak held 1.5 s */
{
    static const char *const N[EN_NMETER] = {"A", "B", "BS", "PG", "OUT"};
    static const int8_t TICK[5] = {-48, -36, -24, -12, 0};
    const uint32_t now = plat_ms();
    const int x0 = PX + 30, w = PW - 34;         /* -54 dB .. +6 dB across */
    char b[8];
    int i;
    for (i = 0; i < 5; i++) {
        const int x = x0 + (TICK[i] + 54) * w / 60;
        cv_rect(x, PY, 1, 66, DE_GRID);
        b[0] = 0;
        de_num(b, TICK[i]);
        cv_text(x - text_w(&FONT_T_XS, b) / 2, PY + 68, &FONT_T_XS, b, DE_DIM);
    }
    for (i = 0; i < EN_NMETER; i++) {
        const int y = PY + 1 + i * 13;
        const float v = en_meter[i];
        float db, hd;
        en_meter[i] = 0.0f;                      /* (taken: the engine fills it again) */
        de.mpk[i] = v > de.mpk[i] ? v : de.mpk[i] * 0.86f;
        if (v >= de.mhold[i] || now - de.mhold_t[i] > 1500u)
            de.mhold[i] = v, de.mhold_t[i] = now;
        db = 6.0206f * fm_log2f(fm_maxf(de.mpk[i], 1e-5f));
        hd = 6.0206f * fm_log2f(fm_maxf(de.mhold[i], 1e-5f));
        cv_text(PX, y - 1, &FONT_T_XS, N[i], i == en_sel ? DE_OR : DE_FG);
        cv_rect(x0, y, w, 10, DE_LINE);
        if (db > -54.0f) {
            const int bw = (int)((fm_minf(db, 6.0f) + 54.0f) * (float)w * (1.0f / 60.0f)), z = 54 * w / 60;
            cv_rect(x0, y, bw < z ? bw : z, 10, DE_OR);
            if (bw > z)                          /* (over full scale: into the clip) */
                cv_rect(x0 + z, y, bw - z, 10, DE_FG);
        }
        if (hd > -54.0f)
            cv_rect(x0 + (int)((fm_minf(hd, 6.0f) + 54.0f) * (float)w * (1.0f / 60.0f)) - 1, y, 2, 10, DE_FG);
    }
}
static void de_pic_cpu(void)                     /* the audio's share of its time: the mean, the heaviest block lately */
{
    char b[24];
    b[0] = 0;
    de_cat(b, "CPU ");
    de_num(b + de_len(b), (int)plat_cpu_pct());
    de_cat(b, "%  PEAK ");
    de_num(b + de_len(b), (int)plat_cpu_peak());
    de_cat(b, "%");
    cv_text(PX, PY + PH - 12, &FONT_T_XS, b, plat_cpu_peak() > 85u ? DE_OR : DE_DIM);
}
static void de_pic_eq(void)                      /* the track's EQ, 20 Hz .. 20 kHz, +-18 dB: the curve as heard, the bands */
{
    static const float TICK[3] = {100.0f, 1000.0f, 10000.0f};
    static const char *const TN[3] = {"100", "1K", "10K"};
    const int ym = PY + PH / 2;
    int i, py = 0;
    for (i = 0; i < 3; i++) {
        const int x = PX + (int)((float)PW * fm_log2f(TICK[i] / 20.0f) / 9.966f);
        cv_rect(x, PY, 1, PH, DE_GRID);
        cv_text(x + 2, PY + PH - 12, &FONT_T_XS, TN[i], DE_DIM);
    }
    cv_rect(PX, ym, PW, 1, DE_LINE);             /* (0 dB) */
    cv_text(PX, PY, &FONT_T_XS, "+18", DE_DIM);
    for (i = 0; i < PW; i += 2) {
        const float hz = 20.0f * fm_exp2f(9.966f * (float)i / (float)PW);
        const int y = ym - (int)(fm_clampf(en_eq_db(hz), -18.0f, 18.0f) * (float)(PH / 2 - 1) * (1.0f / 18.0f));
        if (i)
            cv_line(PX + i - 2, py, PX + i, y, DE_OR);
        py = y;
    }
    for (i = 0; i < EN_NEQ; i++) {               /* each band: a mark at its frequency and gain; the page's, lit */
        const int fv = en_mx[MX_EQ + i * EQ_NF + EQ_FREQ], gv = en_mx[MX_EQ + i * EQ_NF + EQ_GAIN];
        const int x = PX + fv * PW / 100, y = ym - gv * (PH / 2 - 1) / 18;
        if (i == de_band()) {
            cv_rect(x, PY, 1, PH, DE_ORD);
            cv_rect(x - 2, y - 2, 5, 5, DE_FG);
        } else {
            cv_rect(x - 1, y - 1, 3, 3, DE_DIM);
        }
    }
}
static void de_pic_random(void)                  /* the track's pattern as it is, small; what EDIT does here */
{
    const int n = de_nsteps();
    int i;
    for (i = 0; i < n; i++) {
        const int r = i / 16, c = i % 16, x = PX + c * 14, y = PY + 4 + r * 14;
        cv_rect(x, y, 12, 10, en_seq[i].on ? mix(DE_ORD, DE_OR, 8 + 8 * (int)en_seq[i].deg / 6) : (c & 3) ? DE_GRID : DE_LINE);
        if (en_seq_playing && i == en_seq_pos)
            cv_rect(x, y + 11, 12, 1, DE_FG);
    }
    cv_text(PX, PY + PH - 26, &FONT_T_XS, "EDIT  A NEW PATTERN     HELD  UNDO", DE_DIM);
    cv_text(PX, PY + PH - 12, &FONT_T_XS, "SAVE  SOUND      ARP  DELAY", DE_DIM);
}
static void de_pic_voice(void)                   /* the bass's or the ping's voice as struck: two cycles */
{
    static float v[112];
    en_voice_wave(v, 112);
    cv_rect(PX, PY + PH / 2, PW, 1, DE_GRID);
    de_plot(PX, PY, PW, PH, v, 112, DE_OR);
}
static void de_pic_shape(void)                   /* the note's level over 2 s: the attack, the decay (the bass let go at 1 s) */
{
    const float att = 0.0005f * fm_exp2f((float)en_vp[VP_ATT] * 0.0864f), dec = 0.05f * fm_exp2f((float)en_vp[VP_DEC] * 0.0664f);
    const int bass = en_sel == EN_T_BASS, hold = bass && en_vp[VP_DEC] >= 100;
    float lv = 0.0f, at1 = 0.0f;
    int i, py = PY + PH;
    for (i = 0; i < PW; i++) {
        const float t = 2.0f * (float)i / (float)PW;
        int y;
        if (bass && t >= 1.0f) {
            if (at1 <= 0.0f)
                at1 = lv;
            lv = at1 * fm_expf(-6.9f * (t - 1.0f) / 0.04f);
        } else if (t < att) {
            lv = t / att;
        } else {
            lv = hold ? 1.0f : fm_expf(-6.9f * (t - att) / dec);
        }
        y = PY + PH - (int)(lv * (float)(PH - 4));
        if (i)
            cv_line(PX + i - 1, py, PX + i, y, DE_OR);
        py = y;
    }
    if (bass)
        cv_rect(PX + PW / 2, PY, 1, PH, DE_LINE);   /* (the note off) */
    cv_rect(PX, PY + PH, PW, 1, DE_LINE);
    de_axis(PX, PY + PH + 1, PW, 2.0f, 1.0f, "S");
}
static void de_pic_tone(void)                    /* the low-pass, 20 Hz .. 20 kHz, 0 .. -36 dB */
{
    static const float TICK[3] = {100.0f, 1000.0f, 10000.0f};
    static const char *const TN[3] = {"100", "1K", "10K"};
    const float fc = 40.0f * fm_exp2f((float)en_vp[VP_CUT] * 0.0864f);
    int i, py = 0;
    for (i = 0; i < 3; i++) {
        const int x = PX + (int)((float)PW * fm_log2f(TICK[i] / 20.0f) / 9.966f);
        cv_rect(x, PY, 1, PH, DE_GRID);
        cv_text(x + 2, PY + PH - 12, &FONT_T_XS, TN[i], DE_DIM);
    }
    for (i = 0; i < PW; i += 2) {
        const float x = 20.0f * fm_exp2f(9.966f * (float)i / (float)PW) / fc, x2 = x * x;
        const float db = en_vp[VP_CUT] >= 100 ? 0.0f : -3.0103f * fm_log2f(1.0f + x2 * x2);
        const int y = PY + 8 + (int)(fm_clampf(-db, 0.0f, 36.0f) * (float)(PH - 10) * (1.0f / 36.0f));
        if (i)
            cv_line(PX + i - 2, py, PX + i, y, DE_OR);
        py = y;
    }
}
static void de_pic_pdelay(void)                  /* the ping's repeats in seconds: each as Feedback leaves it */
{
    static const float BEATS[EN_NDTIME] = {0.25f, 0.3333f, 0.5f, 0.75f, 0.6667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    float t = BEATS[en_vp[VP_DTIME] % EN_NDTIME] * 60.0f / (float)de.tempo, lv = (float)en_vp[VP_DMIX] * 0.01f;
    const float fb = (float)en_vp[VP_DFB] * 0.0095f, span = 3.0f;
    char b[12];
    int n;
    t = t > 0.999f ? 0.999f : t;
    cv_rect(PX, PY + PH, PW, 1, DE_LINE);
    cv_rect(PX, PY + 4, 3, PH - 4, DE_FG);       /* (the ping) */
    for (n = 1; (float)n * t < span && lv > 0.02f; n++, lv *= fb)
        cv_rect(PX + (int)((float)PW * (float)n * t / span), PY + PH - (int)((float)(PH - 4) * lv), 3, (int)((float)(PH - 4) * lv), DE_OR);
    de_axis(PX, PY + PH + 1, PW, span, 1.0f, "S");
    b[0] = 0;
    de_num(b, (int)(t * 1000.0f + 0.5f));
    de_cat(b, " MS");
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
        case DE_DELAY: case DE_DELAY2: de_pic_delay(); break;
        case DE_MASTER: de_pic_master(), de_pic_cpu(); break;
        case DE_EQ: de_pic_eq(); break;
        case DE_RANDOM: de_pic_random(); break;
        case DE_V1: de_pic_voice(); break;
        case DE_V2: de_pic_shape(); break;
        case DE_V3: de_pic_tone(); break;
        case DE_V4: de_pic_pdelay(); break;
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
        if (en_seq[i].on && (en_seq[i].lock[0] | en_seq[i].lock[1] | en_seq[i].lock[2]))   /* (a locked step: a mark) */
            cv_rect(x + 8, y + 2, 2, 2, DE_BG);
    }
    if (held) {                                  /* the step held: its locks, on ALGORITHM, PRESETS and SELECT */
        static const char *const N[EN_NLK] = {"ALG LEVEL", "PRE CUTOFF", "SEL DELAY"};
        en_step_t t;
        de_step_get(de.step, &t);
        for (i = 0; i < EN_NLK; i++) {
            const int x = 8 + i * 78;
            cv_text(x, DE_MAIN - 30, &FONT_T_XS, N[i], DE_DIM);
            b[0] = 0;
            if (t.lock[i])
                de_num(b, t.lock[i] - 1);
            else
                de_cat(b, "FREE");
            cv_text(x, DE_MAIN - 17, &FONT_T_B, b, t.lock[i] ? DE_OR : DE_LINE);
        }
    }
    cv_blit(0, DE_HDR);
}

static int de_bar_frac(int id, int *bip)         /* a control's place in its range, 0..1000 (bipolar: -1000..1000) */
{
    int lo, hi, v;
    *bip = 0;
    if (id < 0)
        return 0;
    de_param_range(id, &lo, &hi);
    v = de_param_get(id);
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
        lab[0] = "Tempo", lab[1] = "Root", lab[2] = "Mode", lab[3] = de_voice_trk() ? "" : "Voicing";
        de_num(val[0], de.tempo);
        de_cat(val[1], EN_NOTE_NAME[de.root[en_sel] % 12]);
        de_upper(val[2], EN_MODE_NAME[de.mode[en_sel] % EN_NMODES], 12);
        if (!de_voice_trk())
            de_cat(val[3], DE_VO_NAME[de.voi[en_sel]]);
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
    b |= 1u << (de.page == DE_PLAY ? B_HOME : de.page == DE_PATTERN ? B_SEQ : de.page == DE_RANDOM ? B_EDIT : de.page >= DE_MASTER ? B_GLO : de.page >= DE_DELAY ? B_FX
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
        const uint32_t ap = de_pool_abs();
        for (i = 0; i < NKEYS; i++)
            if (ap >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
    }
    if (de.btn & (1u << B_SEQ | 1u << B_ENV)) {  /* SEQ or ENV held: the first eight white keys, the slots with something */
        k = 0;
        for (i = 0; i < DE_NSLOT; i++)
            if (de.btn >> B_SEQ & 1u ? de_slots.pat[i].used : de_slots.snd[i].used)
                k |= 1u << DE_WHITE[i];
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
    if (sum == de_saved_sum || en_seq_playing || de.btn || de.keys || now - de.act_t < 3000u ||
        en_pad_level > 0.01f)
        return;
    de_save();
}

static void de_frame(void)
{
    if (de.spage >= de_spages())                /* (the pattern made shorter) */
        de.spage = (uint8_t)(de_spages() - 1);
    de_input();
    de_autosave();
    de_draw_header();
    de_draw_main();
    de_draw_foot();
    de_leds();
}
