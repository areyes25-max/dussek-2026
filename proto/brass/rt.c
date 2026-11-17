/* The model in blocks, for playing live: lips.c's loop with its state kept, then what sim.radiate does
   to whole files as streaming filters. The mouthpiece pressure from the modes steepens in a short
   delay line, comes down to the base rate, gains the part of the impedance the modes leave out
   (FIR from the flow) and leaves through the bell (FIR, with the microphone's correction). A note
   change crossfades the filters over 10 ms, as the offline crossfade does.
   A reed plays the same loop with a reed for the lips (one closing mode) and three pieces the lips do without: the
   tongue (holds the reed shut, closes the opening under it), the seed (the tongue's push as it lets go) and the glide
   (a fingering change carries the air column's vibration from the old modes into the new). The lips pass no tongue,
   start no seed and call no glide: they run the loop as it was (lips_loop), bit for bit. A reed radiates its flow
   through each note's own Z H (rt_filters_flow), not the loop's pressure through the bell. */
#include <math.h>
#include <complex.h>
#include <string.h>
#include <stdlib.h>

#define OS 4
#define MAXM 64
#define HMAX 4096
#define DMAX 256
#define SD 32
#define SD_D 24
#define FADE 441  /* a fingering change's glide, base samples (10 ms) */
#define FLOWS 3   /* a reed's notes radiating at once: the one sounding and two still ringing out (a trill) */

typedef struct {
    double complex x[MAXM];
    double ly[8], lv[8];
    double uprev, pprev;
    long noise_pos;
    double sd[SD];
    int si;
    double dp[2 * DMAX], du[2 * DMAX];
    int dpos;
    double ht[2 * HMAX], hu[2 * HMAX];
    int hpos_t, hpos_u;
    const double *h, *r, *h_old, *r_old;
    int nh, nr, nh_old, nr_old, fade;
    /* a reed's output (rt_filters_flow): each note's share of the flow through its own Z H, the modal pressure left
       out. FLOWS slots, each a note's filter, its flow history (its weighted share of the flow, base rate), the base
       samples it still sounds for (its fade, then its filter's length), and whether its share rises or falls */
    int flow, fcur, fade_n;
    double hp_b[3], hp_a[2], hp_z[2], hp_sr;  /* the flow's DC blocker (flow_out): a biquad and its rate */
    const double *zh[FLOWS];
    int nzh[FLOWS], fpos[FLOWS], ftail[FLOWS];
    double w0[FLOWS];  /* an old slot's share when the last change came: it falls from there as w0 (1 - g) */
    double fu[FLOWS][2 * HMAX];
    /* the seed (rt_seed): a push on the reed mode at the note's frequency under a Hann, seed_n oversampled steps */
    double seed_amp, seed_w;  /* m/s at its peak; the phase per oversampled step, rad */
    long seed_k, seed_n;      /* steps done, steps in all (0: none) */
    /* the glide (rt_glide): the old bank and the state of its unpaired modes, kept for FADE base samples */
    double complex Eo[MAXM], ro[MAXM], xo[MAXM], LEo[MAXM];  /* poles, residues, states, log of the poles */
    double Go, fo, fn, pair_max, pair_tol;  /* the old bank's sum of 2 Re r; the old and new notes' Hz; the pairing's */
    int mo, glide, pending, gk;  /* the old bank's modes; gliding; the pairing waits for the new bank; steps done */
    int partner[MAXM];  /* per new mode, the old one it glides from (-1: none, it starts from rest) */
    int taken[MAXM];    /* per old mode, the new one it glides into (-1: none, it fades out on its own) */
} RT;

static inline double dot(const double *a, const double *b, int n)
{
    double t = 0.0;
    for (int i = 0; i < n; i++) t += a[i] * b[i];
    return t;
}

RT *rt_new(void) { return calloc(1, sizeof(RT)); }
void rt_free(RT *s) { free(s); }

void rt_filters(RT *s, const double *h, int nh, const double *r, int nr)
{
    s->h_old = s->h; s->nh_old = s->nh; s->r_old = s->r; s->nr_old = s->nr;
    s->h = h; s->nh = nh; s->r = r; s->nr = nr;
    s->fade = s->h_old ? 441 : 0;
}

/* A reed's radiation, the spike's loop.radiate: each note's share of the flow (at the base rate) through its own
   flow-to-air filter zh (Z H, the mouthpiece's whole impedance times the bell's pressure transfer, nzh <= HMAX taps);
   the modal pressure, the residual and the bell's filter play no part (the loop still sees the modal pressure). A
   fingering change gives the new note the flow's share rising over FADE base samples, the old one the rest: the old
   filter rings out its own past flow (its tail: FADE plus its length), the new one hears only the flow since the
   change. Radiating the loop's pressure instead sent the gliding modes' sweep out through the bell, 10-15 dB over
   6 kHz at a slur; fading the two filters' outputs on one flow cut the old one's ringing (slurs dipped 9-10 dB). A
   change with every slot sounding takes the one nearest its end (its share, if it still had one, is lost: the port
   keeps this rule; FLOWS 3 covers two changes within one fade). At every change each old slot keeps the share it had
   (w0) and falls from it as w0 (1 - g) while the new one rises as g: the shares sum to 1 however close the changes
   (from 1 - g alone, two changes within the fade summed to up to 2 and a falling slot jumped back to 1). Once called,
   the state stays the reed's: rt_process runs reed_loop. Returns 0, or -1 (nothing changed) for a filter over HMAX
   taps: never cut short. (Engine: Task 9/10 port this as FLOWS FIRs on per-note flow histories, no allocation.) */
int rt_filters_flow(RT *s, const double *zh, int nzh)
{
    if (nzh > HMAX || nzh < 1) return -1;
    int c = 0;
    if (s->flow) {
        double g = s->fade_n > 0 ? 1.0 - (double)s->fade_n / FADE : 1.0;  /* the sounding note's share now */
        for (int k = 0; k < FLOWS; k++) {  /* what every old slot has now: from here it falls over the new fade */
            if (k == s->fcur) continue;
            double w = s->ftail[k] > s->nzh[k] ? s->w0[k] * (1.0 - g) : 0.0;
            s->w0[k] = w;
            if (w > 0.0) s->ftail[k] = FADE + s->nzh[k];
        }
        s->w0[s->fcur] = g;
        s->ftail[s->fcur] = FADE + s->nzh[s->fcur];  /* the old note: its share falls, then it rings out */
        for (int k = 0; k < FLOWS; k++)  /* the slot that ends first, not the one sounding */
            if (k != s->fcur && (c == s->fcur || s->ftail[k] < s->ftail[c])) c = k;
        s->fade_n = FADE;
    } else {
        for (int k = 0; k < FLOWS; k++) s->ftail[k] = 0, s->w0[k] = 0.0;
        s->fade_n = 0;
    }
    memset(s->fu[c], 0, sizeof(s->fu[c]));
    s->fpos[c] = 0;
    s->zh[c] = zh;
    s->nzh[c] = nzh;
    s->ftail[c] = 0;
    s->w0[c] = 0.0;
    s->fcur = c;
    s->flow = 1;
    return 0;
}

#define FLOW_HP 30.0  /* Hz: the flow's DC blocker (flow_out) */

/* a reed's output sample from the base-rate flow ub: each sounding note's share through its zh. The flow goes first
   through a 2nd-order high-pass at FLOW_HP (0.01 dB at D3's 147 Hz): the exact Z H vanishes at DC, cut to its taps it
   keeps a gain below 40 Hz, and the flow's step from rest to its mean as the tongue lets go came out as a thump (2-6 dB
   over the note for 50 ms on the clarion's pp: a start read as 80-110 ms early). */
static double flow_out(RT *s, double ub, double sr)
{
    if (s->hp_sr != sr) {  /* RBJ's Butterworth high-pass, transposed direct form II */
        double w = 2.0 * M_PI * FLOW_HP / sr, c = cos(w), al = sin(w) / (2.0 * M_SQRT1_2), a0 = 1.0 + al;
        s->hp_b[0] = s->hp_b[2] = (1.0 + c) / 2.0 / a0; s->hp_b[1] = -(1.0 + c) / a0;
        s->hp_a[0] = -2.0 * c / a0; s->hp_a[1] = (1.0 - al) / a0;
        s->hp_z[0] = s->hp_z[1] = 0.0;
        s->hp_sr = sr;
    }
    double v = s->hp_b[0] * ub + s->hp_z[0];
    s->hp_z[0] = s->hp_b[1] * ub - s->hp_a[0] * v + s->hp_z[1];
    s->hp_z[1] = s->hp_b[2] * ub - s->hp_a[1] * v;
    ub = v;
    double g = s->fade_n > 0 ? 1.0 - (double)s->fade_n / FADE : 1.0, y = 0.0;  /* the new note's share */
    if (s->fade_n > 0) s->fade_n--;
    for (int k = 0; k < FLOWS; k++) {
        if (k != s->fcur && s->ftail[k] <= 0) continue;
        double share = k == s->fcur ? g : (s->ftail[k] > s->nzh[k] ? s->w0[k] * (1.0 - g) : 0.0);
        s->fpos[k] = s->fpos[k] == 0 ? HMAX - 1 : s->fpos[k] - 1;
        s->fu[k][s->fpos[k]] = s->fu[k][s->fpos[k] + HMAX] = ub * share;
        y += dot(s->zh[k], s->fu[k] + s->fpos[k], s->nzh[k]);
        if (k != s->fcur) s->ftail[k]--;
    }
    return y;
}

/* The tongue's push as it lets go of the reed: for seed_s seconds each oversampled step adds
   -amp * hann * sin(2 pi f t) * 2 pi f / (sr * OS) to the reed mode's speed, a push at the note's frequency that the
   bore's mode grows from (an impulse clicked). The hann is numpy's over the steps: 0.5 - 0.5 cos(2 pi k / (n - 1)). */
void rt_seed(RT *s, double amp, double f, double seed_s, double sr)
{
    s->seed_amp = amp;
    s->seed_w = 2.0 * M_PI * f / (sr * OS);
    s->seed_n = lround(seed_s * sr * OS);
    s->seed_k = 0;
}

/* A fingering change: called before the first rt_process with the new bank, with the old bank (E and r per
   oversampled step, as rt_process takes them) and both notes' frequencies. The old bank and the state are kept; the
   first rt_process pairs each new mode with the old one at its harmonic number (fm = log(arg(E) / (2 pi T) / f), only
   fm < log(pair_max), the nearest within pair_tol, an old mode given once) and the pairs glide from the old to the
   new over FADE base samples: one mode at full weight whose log pole and residue move linearly, its state the old
   mode's. An unpaired new mode starts from rest at weight a (the glide's progress), an unpaired old one goes on at
   1 - a; then the old bank is dropped. The new bank's count of modes (rt_process's m) stays the same during the glide.
   A glide called during a glide starts over from the bank sounding: the first glide's unpaired old modes stop. */
void rt_glide(RT *s, int m_old, const double *Er_old, const double *Ei_old, const double *rr_old, const double *ri_old,
              double f_old, double f_new, double pair_max, double pair_tol)
{
    if (m_old > MAXM) m_old = MAXM;
    s->mo = m_old;
    s->Go = 0.0;
    for (int i = 0; i < m_old; i++) {
        s->Eo[i] = Er_old[i] + I * Ei_old[i];
        s->ro[i] = rr_old[i] + I * ri_old[i];
        s->LEo[i] = clog(s->Eo[i]);
        s->xo[i] = s->x[i];
        s->Go += 2.0 * rr_old[i];
    }
    s->fo = f_old; s->fn = f_new; s->pair_max = pair_max; s->pair_tol = pair_tol;
    s->glide = 1; s->pending = 1; s->gk = 0;
}

/* the pairing, at the first block of the new bank */
static void pair(RT *s, int m, const double complex *E, double T)
{
    double fmo[MAXM], top = log(s->pair_max);
    for (int i = 0; i < MAXM; i++) s->partner[i] = s->taken[i] = -1;
    for (int i = 0; i < s->mo; i++) fmo[i] = log(carg(s->Eo[i]) / (2.0 * M_PI * T) / s->fo);
    for (int j = 0; j < m; j++) {
        s->x[j] = 0.0;
        double fm = log(carg(E[j]) / (2.0 * M_PI * T) / s->fn);
        if (!(fm < top)) continue;
        int best = -1;
        double bd = s->pair_tol;
        for (int i = 0; i < s->mo; i++)
            if (s->taken[i] < 0 && fmo[i] < top && fabs(fmo[i] - fm) < bd) { bd = fabs(fmo[i] - fm); best = i; }
        if (best >= 0) { s->partner[j] = best; s->taken[best] = j; s->x[j] = s->xo[best]; }
    }
    s->pending = 0;
}


/* The loop with the reed's pieces: the opening's width ramps, the tongue (0 off the reed, 1 holding it shut), the
   seed, the glide. */
static void reed_loop(RT *s, int n, double pm0, double pm1, double lip0, double lip1, double burst0, double burst1,
                double width0, double width1, double tongue0, double tongue1,
                double kick, double mu, int m, const double *Er, const double *Ei, const double *rr, const double *ri,
                int nlip, const double *lratio, const double *lq, const double *lsign, const double *lcomp,
                double y0, double soft, double rho, double sr,
                const double *noise, long nnoise, double nl, const double *dec, int ndec, double *out)
{
    double T = 1.0 / (sr * OS), G = 0.0;
    double complex E[MAXM], r[MAXM];
    for (int j = 0; j < m; j++) {
        E[j] = Er[j] + I * Ei[j]; r[j] = rr[j] + I * ri[j];
        G += 2.0 * rr[j];
    }
    double complex LE[MAXM];  /* the log of the new poles, for the pairs' glide */
    if (s->glide) {
        if (s->pending) pair(s, m, E, T);
        for (int j = 0; j < m; j++) if (s->partner[j] >= 0) LE[j] = clog(E[j]);
    }
    int total = n * OS;
    for (int i = 0; i < n; i++) {
        for (int o = 0; o < OS; o++) {
            int k = i * OS + o;
            double w = (double)k / total;
            double pm = pm0 + (pm1 - pm0) * w, lf = lip0 + (lip1 - lip0) * w, bu = burst0 + (burst1 - burst0) * w;
            double wd = width0 + (width1 - width0) * w, tg = tongue0 + (tongue1 - tongue0) * w;
            double dp = pm - s->pprev, y = y0;
            for (int l = 0; l < nlip; l++) {
                double wl = 2.0 * M_PI * lf * lratio[l];
                s->lv[l] += T * (lsign[l] * lcomp[l] * dp / mu - wl / lq[l] * s->lv[l] - wl * wl * s->ly[l]);
                if (l == 0 && kick != 0.0) s->lv[l] += kick / total;  /* over the block: at once it clicks */
                if (l == 0 && s->seed_k < s->seed_n) {  /* the seed */
                    double hann = 0.5 - 0.5 * cos(2.0 * M_PI * s->seed_k / (s->seed_n - 1 > 0 ? s->seed_n - 1 : 1));
                    s->lv[l] -= s->seed_amp * hann * sin(s->seed_w * s->seed_k) * s->seed_w;
                    s->seed_k++;
                }
                s->ly[l] += T * s->lv[l];
                if (tg >= 1.0) { s->ly[l] = l == 0 ? -y0 : 0.0; s->lv[l] = 0.0; }  /* the tongue holds the reed shut */
                y += s->ly[l];
            }
            double S = wd * 0.5 * (y + sqrt(y * y + soft * soft));
            S *= 1.0 - tg;  /* the opening closes under the tongue, opens as it lets go */
            double ph = 0.0, Gk = G, ga = 0.0;
            double complex xh[MAXM], xho[MAXM], rk[MAXM];
            if (s->glide) {
                /* the glide: the pairs at full weight with their pole and residue between the old and the new, the
                   unpaired new modes at ga, the unpaired old ones at 1 - ga */
                ga = (double)(s->gk + 1) / (FADE * OS);
                Gk = (1.0 - ga) * s->Go + ga * G;
                for (int j = 0; j < m; j++) {
                    int i = s->partner[j];
                    if (i >= 0) {
                        double complex Ek = cexp((1.0 - ga) * s->LEo[i] + ga * LE[j]);
                        rk[j] = (1.0 - ga) * s->ro[i] + ga * r[j];
                        xh[j] = Ek * s->x[j] + rk[j] * s->uprev * Ek;
                        ph += 2.0 * creal(xh[j]);
                    } else {
                        rk[j] = r[j];
                        xh[j] = E[j] * s->x[j] + r[j] * s->uprev * E[j];
                        ph += ga * 2.0 * creal(xh[j]);
                    }
                }
                for (int i = 0; i < s->mo; i++) {
                    if (s->taken[i] >= 0) continue;
                    xho[i] = s->Eo[i] * s->xo[i] + s->ro[i] * s->uprev * s->Eo[i];
                    ph += (1.0 - ga) * 2.0 * creal(xho[i]);
                }
            } else {
                for (int j = 0; j < m; j++) {
                    xh[j] = E[j] * s->x[j] + r[j] * s->uprev * E[j];
                    ph += 2.0 * creal(xh[j]);
                }
            }
            double P = pm - ph, uk = 0.0;
            if (S > 0.0) {
                double b = 2.0 * S * S * Gk / rho, c = 2.0 * S * S * fabs(P) / rho;
                uk = copysign(-b / 2.0 + sqrt(b * b / 4.0 + c), P);
            }
            uk += noise[s->noise_pos] * bu * fabs(uk);
            if (++s->noise_pos >= nnoise) s->noise_pos = 0;
            if (s->glide) {
                for (int j = 0; j < m; j++) s->x[j] = xh[j] + rk[j] * uk;
                for (int i = 0; i < s->mo; i++) if (s->taken[i] < 0) s->xo[i] = xho[i] + s->ro[i] * uk;
                if (++s->gk >= FADE * OS) {  /* the glide's end: the old bank goes, the pairs run as the new modes */
                    s->glide = 0;
                    for (int j = 0; j < MAXM; j++) s->partner[j] = -1;
                }
            } else {
                for (int j = 0; j < m; j++) s->x[j] = xh[j] + r[j] * uk;
            }
            double pk = ph + Gk * uk;
            s->uprev = uk; s->pprev = pk;
            /* steepening: a sample arrives earlier the higher its pressure */
            s->sd[s->si] = pk;
            int ri0 = (s->si - SD_D + SD) % SD;
            double adv = nl * s->sd[ri0];
            if (adv > SD_D - 2) adv = SD_D - 2;
            if (adv < -(SD - SD_D - 2)) adv = -(SD - SD_D - 2);
            double pos = (double)(s->si - SD_D + SD) + adv;
            int a = (int)floor(pos);
            double fr = pos - a;
            double ys = s->sd[a % SD] * (1.0 - fr) + s->sd[(a + 1) % SD] * fr;
            s->si = (s->si + 1) % SD;
            /* the anti-alias FIR's history, for the base rate */
            s->dpos = s->dpos == 0 ? ndec - 1 : s->dpos - 1;
            s->dp[s->dpos] = s->dp[s->dpos + ndec] = ys;
            s->du[s->dpos] = s->du[s->dpos + ndec] = uk;
        }
        double pb = dot(dec, s->dp + s->dpos, ndec), ub = dot(dec, s->du + s->dpos, ndec);
        if (s->flow) {  /* a reed's output: each note's share of the flow through its own Z H (rt_filters_flow) */
            out[i] = flow_out(s, ub, sr);
            continue;
        }
        s->hpos_u = s->hpos_u == 0 ? HMAX - 1 : s->hpos_u - 1;
        s->hu[s->hpos_u] = s->hu[s->hpos_u + HMAX] = ub;
        double a = dot(s->r, s->hu + s->hpos_u, s->nr);
        if (s->fade > 0) {  /* the part the modes leave out crossfades with the bell's filter */
            double g = s->fade / 441.0;
            a = a * (1.0 - g) + dot(s->r_old, s->hu + s->hpos_u, s->nr_old) * g;
        }
        a += pb;
        s->hpos_t = s->hpos_t == 0 ? HMAX - 1 : s->hpos_t - 1;
        s->ht[s->hpos_t] = s->ht[s->hpos_t + HMAX] = a;
        double y = dot(s->h, s->ht + s->hpos_t, s->nh);
        if (s->fade > 0) {
            double yo = dot(s->h_old, s->ht + s->hpos_t, s->nh_old);
            double g = s->fade / 441.0;
            y = y * (1.0 - g) + yo * g;
            s->fade--;
        }
        out[i] = y;
    }
}

/* The loop without them, the lips' as it was: with -ffast-math the reed's pieces in the same loop, at work or not,
   changed how its sums compile and moved the lips' last bits. Without the pieces both loops are the same sums. */
static __attribute__((noinline)) void lips_loop(RT *s, int n, double pm0, double pm1, double lip0, double lip1, double burst0, double burst1,
                double kick, double mu, int m, const double *Er, const double *Ei, const double *rr, const double *ri,
                int nlip, const double *lratio, const double *lq, const double *lsign, const double *lcomp,
                double width, double y0, double soft, double rho, double sr,
                const double *noise, long nnoise, double nl, const double *dec, int ndec, double *out)
{
    double T = 1.0 / (sr * OS), G = 0.0;
    double complex E[MAXM], r[MAXM];
    for (int j = 0; j < m; j++) {
        E[j] = Er[j] + I * Ei[j]; r[j] = rr[j] + I * ri[j];
        G += 2.0 * rr[j];
    }
    int total = n * OS;
    for (int i = 0; i < n; i++) {
        for (int o = 0; o < OS; o++) {
            int k = i * OS + o;
            double w = (double)k / total;
            double pm = pm0 + (pm1 - pm0) * w, lf = lip0 + (lip1 - lip0) * w, bu = burst0 + (burst1 - burst0) * w;
            double dp = pm - s->pprev, y = y0;
            for (int l = 0; l < nlip; l++) {
                double wl = 2.0 * M_PI * lf * lratio[l];
                s->lv[l] += T * (lsign[l] * lcomp[l] * dp / mu - wl / lq[l] * s->lv[l] - wl * wl * s->ly[l]);
                if (l == 0 && kick != 0.0) s->lv[l] += kick / total;  /* over the block: at once it clicks */
                s->ly[l] += T * s->lv[l];
                y += s->ly[l];
            }
            double S = width * 0.5 * (y + sqrt(y * y + soft * soft));
            double ph = 0.0;
            double complex xh[MAXM];
            for (int j = 0; j < m; j++) {
                xh[j] = E[j] * s->x[j] + r[j] * s->uprev * E[j];
                ph += 2.0 * creal(xh[j]);
            }
            double P = pm - ph, uk = 0.0;
            if (S > 0.0) {
                double b = 2.0 * S * S * G / rho, c = 2.0 * S * S * fabs(P) / rho;
                uk = copysign(-b / 2.0 + sqrt(b * b / 4.0 + c), P);
            }
            uk += noise[s->noise_pos] * bu * fabs(uk);
            if (++s->noise_pos >= nnoise) s->noise_pos = 0;
            for (int j = 0; j < m; j++) s->x[j] = xh[j] + r[j] * uk;
            double pk = ph + G * uk;
            s->uprev = uk; s->pprev = pk;
            /* steepening: a sample arrives earlier the higher its pressure */
            s->sd[s->si] = pk;
            int ri0 = (s->si - SD_D + SD) % SD;
            double adv = nl * s->sd[ri0];
            if (adv > SD_D - 2) adv = SD_D - 2;
            if (adv < -(SD - SD_D - 2)) adv = -(SD - SD_D - 2);
            double pos = (double)(s->si - SD_D + SD) + adv;
            int a = (int)floor(pos);
            double fr = pos - a;
            double ys = s->sd[a % SD] * (1.0 - fr) + s->sd[(a + 1) % SD] * fr;
            s->si = (s->si + 1) % SD;
            /* the anti-alias FIR's history, for the base rate */
            s->dpos = s->dpos == 0 ? ndec - 1 : s->dpos - 1;
            s->dp[s->dpos] = s->dp[s->dpos + ndec] = ys;
            s->du[s->dpos] = s->du[s->dpos + ndec] = uk;
        }
        double pb = dot(dec, s->dp + s->dpos, ndec), ub = dot(dec, s->du + s->dpos, ndec);
        s->hpos_u = s->hpos_u == 0 ? HMAX - 1 : s->hpos_u - 1;
        s->hu[s->hpos_u] = s->hu[s->hpos_u + HMAX] = ub;
        double a = dot(s->r, s->hu + s->hpos_u, s->nr);
        if (s->fade > 0) {  /* the part the modes leave out crossfades with the bell's filter */
            double g = s->fade / 441.0;
            a = a * (1.0 - g) + dot(s->r_old, s->hu + s->hpos_u, s->nr_old) * g;
        }
        a += pb;
        s->hpos_t = s->hpos_t == 0 ? HMAX - 1 : s->hpos_t - 1;
        s->ht[s->hpos_t] = s->ht[s->hpos_t + HMAX] = a;
        double y = dot(s->h, s->ht + s->hpos_t, s->nh);
        if (s->fade > 0) {
            double yo = dot(s->h_old, s->ht + s->hpos_t, s->nh_old);
            double g = s->fade / 441.0;
            y = y * (1.0 - g) + yo * g;
            s->fade--;
        }
        out[i] = y;
    }
}

/* n base-rate samples; controls ramp from *0 to *1 across them. The reed's loop while one of its pieces is at work
   (the width moving, the tongue on the reed, a seed, a glide) or its output is the flow's (rt_filters_flow), else the
   lips'. */
void rt_process(RT *s, int n, double pm0, double pm1, double lip0, double lip1, double burst0, double burst1,
                double width0, double width1, double tongue0, double tongue1,
                double kick, double mu, int m, const double *Er, const double *Ei, const double *rr, const double *ri,
                int nlip, const double *lratio, const double *lq, const double *lsign, const double *lcomp,
                double y0, double soft, double rho, double sr,
                const double *noise, long nnoise, double nl, const double *dec, int ndec, double *out)
{
    if (s->flow || width0 != width1 || tongue0 != 0.0 || tongue1 != 0.0 || s->glide || s->seed_k < s->seed_n)
        reed_loop(s, n, pm0, pm1, lip0, lip1, burst0, burst1, width0, width1, tongue0, tongue1, kick, mu, m, Er, Ei, rr, ri,
                  nlip, lratio, lq, lsign, lcomp, y0, soft, rho, sr, noise, nnoise, nl, dec, ndec, out);
    else
        lips_loop(s, n, pm0, pm1, lip0, lip1, burst0, burst1, kick, mu, m, Er, Ei, rr, ri, nlip, lratio, lq, lsign, lcomp,
                  width0, y0, soft, rho, sr, noise, nnoise, nl, dec, ndec, out);
}
