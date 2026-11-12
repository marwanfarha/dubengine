/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine's CPU by stage, block by block (1.4.0: the FM-1's peaks): renders a piece in 256-frame blocks (the
 * FM-1's), several times with the same seed, each block's time the least of the runs (the host's own noise out), and
 * prints the distribution (mean, p99, p99.9, max) and what the heaviest blocks are made of (amb.c's EN_PROF stages).
 *
 *   en_prof steady PIECE SECS [RUNS] [SEED] [NAME=V ...]   the piece at its recipe (and the overrides), Endless;
 *                                               fx=N: the effects grid's effect N held throughout; siren=S: PERFORM's
 *                                               dub siren S (EN_SIR_*) held throughout
 *   en_prof change FROM TO [RUNS]               FROM for 12 s, then TO as the UI sends a piece (every parameter,
 *                                               the pool, a seed, a regrow): the worst blocks around the change
 *
 *   cc -O2 -ffp-contract=off -DEN_HOST -DEN_PROF -Ifirmware/src/dsp -o build/host/en_prof host/en_prof.c \
 *      firmware/src/dsp/amb.c -lm */
#define EN_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }
static double en_prof_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec * 1e-3;
}
uint32_t en_prof_ticks(void)                     /* (amb.c's stages: nanoseconds) */
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec);
}

static const char *const ST[EN_PROF_N] = {"drain", "control", "fad+mix+lfo", "stabs", "bass", "pings", "widen",
                                          "drums", "noise", "fx", "rumble", "mixloop", "events"};
#define NB_MAX 200000
static float tot[NB_MAX], stg[NB_MAX][EN_PROF_N];

static void piece_send(int i)                    /* as the UI's set_piece: every parameter, the pool, a seed, a regrow */
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    int k;
    en_piece(i, par, &pool);
    for (k = 0; k < P_NPARAMS; k++)
        if (k != P_TUNE && k != P_MIDI)
            en_set(k, par[k]);
    en_pool(pool);
}

static int nov, ovp[32], ovv[32];               /* (steady: NAME=V overrides, after the recipe) */
static uint32_t pr_fx;                           /* (steady: fx=N, the grid's effects held) */
static int pr_siren = -1;                        /* (steady: siren=S, a dub siren held) */
static int find_param(const char *name, int len)
{
    int i, k;
    for (i = 0; i < P_NPARAMS; i++) {
        const char *n = en_param_info(i)->name;
        for (k = 0; k < len && n[k]; k++)
            if (n[k] != name[k] && !(n[k] == ' ' && name[k] == '_'))
                break;
        if (k == len && !n[k])
            return i;
    }
    return -1;
}

static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }

static int run(int mode, int a, int b, double secs, unsigned seed, int r)   /* one run; each block's time kept if less */
{
    static int32_t buf[2 * 256];
    int nb = (int)(secs * 44100.0 / 256.0), i, k, change_at = (int)(12.0 * 44100.0 / 256.0);
    en_init();
    piece_send(a);
    for (k = 0; k < nov; k++)
        en_set(ovp[k], ovv[k]);
    en_seed(seed);
    en_regrow();
    en_play(1);
    if (pr_fx)
        en_fx(pr_fx);
    if (pr_siren >= 0)
        en_siren(pr_siren, EN_SIR_TAP), en_siren(pr_siren, EN_SIR_HOLD);
    if (nb > NB_MAX)
        nb = NB_MAX;
    for (i = 0; i < nb; i++) {
        double t0;
        if (mode == 1 && i == change_at) {
            piece_send(b);
            en_seed(seed + 1u);
            en_regrow();
        }
        for (k = 0; k < EN_PROF_N; k++)
            en_prof[k] = 0;
        t0 = en_prof_now();
        en_render(buf, 256, 2048);
        {
            const float t = (float)(en_prof_now() - t0);
            if (r == 0 || t < tot[i]) {
                tot[i] = t;
                for (k = 0; k < EN_PROF_N; k++)
                    stg[i][k] = (float)en_prof[k] * 0.001f;
            }
        }
    }
    return nb;
}

int main(int argc, char **argv)
{
    int mode, a, b = 0, runs = 3, nb = 0, r, i, k;
    double secs;
    unsigned seed = 7;
    static float srt[NB_MAX];
    if (argc < 4) {
        fprintf(stderr, "usage: en_prof steady PIECE SECS [RUNS] [SEED] | en_prof change FROM TO [RUNS]\n");
        return 2;
    }
    mode = !strcmp(argv[1], "change");
    a = atoi(argv[2]);
    if (mode) {
        b = atoi(argv[3]);
        secs = 20.0;
        if (argc > 4)
            runs = atoi(argv[4]);
    } else {
        secs = atof(argv[3]);
        if (argc > 4)
            runs = atoi(argv[4]);
        if (argc > 5)
            seed = (unsigned)atoi(argv[5]);
        for (i = 6; i < argc && nov < 32; i++) {
            const char *eq = strchr(argv[i], '=');
            int p;
            if (!strncmp(argv[i], "fx=", 3)) {
                pr_fx |= 1u << (atoi(argv[i] + 3) & 15);
                continue;
            }
            if (!strncmp(argv[i], "siren=", 6)) {
                pr_siren = atoi(argv[i] + 6);
                continue;
            }
            p = eq ? find_param(argv[i], (int)(eq - argv[i])) : -1;
            if (p < 0) {
                fprintf(stderr, "no parameter %s\n", argv[i]);
                return 2;
            }
            ovp[nov] = p, ovv[nov++] = atoi(eq + 1);
        }
    }
    for (r = 0; r < runs; r++)
        nb = run(mode, a, b, secs, seed, r);
    if (mode) {                                  /* the change: the blocks from it on, and the steady before */
        const int c = (int)(12.0 * 44100.0 / 256.0);
        float before = 0.0f;
        for (i = c - 400; i < c; i++)
            before = tot[i] > before ? tot[i] : before;
        printf("change %s -> %s: the worst block before %.1f us; from the change:\n", en_piece_name(a), en_piece_name(b), before);
        for (i = c; i < c + 8; i++) {
            printf("  block +%d %6.1f us:", i - c, tot[i]);
            for (k = 0; k < EN_PROF_N; k++)
                if (stg[i][k] > 0.5f)
                    printf(" %s %.1f", ST[k], stg[i][k]);
            printf("\n");
        }
        {
            float w = 0.0f;
            int wi = c;
            for (i = c + 8; i < c + 2000 && i < nb; i++)
                if (tot[i] > w)
                    w = tot[i], wi = i;
            printf("  the worst of the next 12 s: %.1f us (block +%d)\n", w, wi - c);
        }
        return 0;
    }
    {
        double sum = 0.0, ss[EN_PROF_N] = {0};
        float p99, p999, mx;
        int top = nb / 1000 > 5 ? nb / 1000 : 5, n = 0;
        float thr;
        for (i = 0; i < nb; i++) {
            sum += tot[i];
            srt[i] = tot[i];
            for (k = 0; k < EN_PROF_N; k++)
                ss[k] += stg[i][k];
        }
        qsort(srt, (size_t)nb, sizeof srt[0], cmpf);
        p99 = srt[(int)(nb * 0.99)], p999 = srt[(int)(nb * 0.999)], mx = srt[nb - 1];
        thr = srt[nb - top];
        printf("%-11s mean %5.1f  p99 %5.1f  p99.9 %5.1f  max %5.1f us\n", en_piece_name(a), sum / nb, p99, p999, mx);
        printf("  mean by stage:");
        for (k = 0; k < EN_PROF_N; k++)
            printf(" %s %.1f", ST[k], ss[k] / nb);
        printf("\n  the %d heaviest blocks, by stage:", top);
        {
            double hs[EN_PROF_N] = {0};
            for (i = 0; i < nb; i++)
                if (tot[i] >= thr) {
                    n++;
                    for (k = 0; k < EN_PROF_N; k++)
                        hs[k] += stg[i][k];
                }
            for (k = 0; k < EN_PROF_N; k++)
                printf(" %s %.1f", ST[k], hs[k] / (n ? n : 1));
        }
        printf("\n");
    }
    return 0;
}
