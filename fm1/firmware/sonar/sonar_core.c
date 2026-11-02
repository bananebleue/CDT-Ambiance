/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* Le cœur du sonar : voir sonar_core.h. Inclus une seule fois dans l'application (compilation unitaire). */
#include "sonar_core.h"
#include "sonar_tables.h"

#ifndef SN_POOL
#define SN_POOL                         /* sur le FM-1 : __attribute__((section(".pool"))) (grands tampons) */
#endif

#define SN_ONE_Q30 (1 << 30)
#define SN_VOICE_Q15 9830               /* niveau d'un ping : 0,30 de la pleine échelle */
#define SN_CONT_Q15 5407                /* bip continu : 0,55 d'un ping */
#define SN_NV 8u

/* ---------------- réglages et état partagés avec la boucle principale */
static volatile sn_settings_t sn_cfg = {0, 20, 3600, 70, 30, 1, 0};
static volatile sn_status_t sn_stat;

#define SN_QN 16u
static volatile uint32_t sn_q_cmd[SN_QN];
static volatile int32_t sn_q_arg[SN_QN];
static volatile uint32_t sn_q_w, sn_q_r;           /* w : la boucle principale ; r : l'interruption */

static void sn_set(const sn_settings_t *s)
{
    sn_cfg.delay_s = s->delay_s > SN_MAX_SECONDS ? SN_MAX_SECONDS : s->delay_s;
    sn_cfg.fade_s = s->fade_s > SN_MAX_SECONDS ? SN_MAX_SECONDS : s->fade_s;
    sn_cfg.duration_s = s->duration_s > SN_MAX_SECONDS ? SN_MAX_SECONDS : s->duration_s;
    sn_cfg.volume = s->volume > 100u ? 100u : s->volume;
    sn_cfg.reverb = s->reverb > 100u ? 100u : s->reverb;
    sn_cfg.type = s->type < SN_NTYPES ? s->type : 1u;
    sn_cfg.tone = s->tone < -24 ? -24 : s->tone > 24 ? 24 : s->tone;
}

static int sn_cmd(uint32_t cmd, int32_t arg)
{
    uint32_t w = sn_q_w;
    if (w - sn_q_r >= SN_QN) return -1;           /* file pleine */
    sn_q_cmd[w % SN_QN] = cmd;
    sn_q_arg[w % SN_QN] = arg;
    sn_q_w = w + 1u;                               /* publié en dernier */
    return 0;
}

static void sn_status(sn_status_t *st)
{
    st->phase = sn_stat.phase;
    st->muted = sn_stat.muted;
    st->wait_left_s = sn_stat.wait_left_s;
    st->remaining_s = sn_stat.remaining_s;
    st->interval_ms = sn_stat.interval_ms;
    st->echo_ms = sn_stat.echo_ms;
    st->pings = sn_stat.pings;
}

/* ---------------- synthèse */
static int32_t sn_sine(uint32_t ph)                /* phase 32 bits -> Q15 (table de 2048 points, interpolée) */
{
    uint32_t i = ph >> 21, f = (ph >> 13) & 255u;
    int32_t a = SN_SINE[i], b = SN_SINE[i + 1u];
    return a + (((b - a) * (int32_t)f) >> 8);
}

enum { SN_PING = 0, SN_PONG, SN_CONTV };
typedef struct {
    uint8_t on, bus, kind, np, stage;              /* stage : 0 montée, 1 décroissance / tenue, 2 relâche */
    uint32_t ph[4], inc[4];
    int32_t g[4];
    int32_t env;                                   /* Q30 */
    uint32_t att, k;                               /* pas de montée, facteur de décroissance Q30 */
    int32_t lvl;                                   /* Q15 */
    int32_t click_env, click_lvl, lo, hp;          /* le clic d'attaque (types Clic et Impulsion) */
    uint32_t hold;                                 /* bip continu de pré-écoute : échantillons avant relâche */
    uint32_t age;
} sn_voice_t;
static sn_voice_t sn_v[SN_NV];
static uint32_t sn_age, sn_noise = 22222u;

/* réverbération mono de type Freeverb : 4 filtres en peigne amortis + 2 passe-tout */
typedef struct {
    int32_t *cb[4], *ap[2];
    uint32_t len[4], idx[4], alen[2], aidx[2];
    int32_t store[4];
    int32_t fb[4];
} sn_rev_t;
static int32_t sn_rev_mem[3][SN_COMB_TOTAL + SN_AP_TOTAL] SN_POOL;
static sn_rev_t sn_rev_main, sn_rev_prev, sn_rev_gen;

static void sn_rev_init(sn_rev_t *r, int32_t *mem, const int16_t *fb)
{
    static const uint32_t CL[4] = SN_COMB_LEN, AL[2] = SN_AP_LEN;
    uint32_t i, j;
    for (i = 0; i < 4u; i++) {
        r->cb[i] = mem; r->len[i] = CL[i]; r->idx[i] = 0; r->store[i] = 0; r->fb[i] = fb[i];
        for (j = 0; j < CL[i]; j++) mem[j] = 0;
        mem += CL[i];
    }
    for (i = 0; i < 2u; i++) {
        r->ap[i] = mem; r->alen[i] = AL[i]; r->aidx[i] = 0;
        for (j = 0; j < AL[i]; j++) mem[j] = 0;
        mem += AL[i];
    }
}

/* produit Q15 arrondi vers zéro : dans une boucle de réverbération, un arrondi vers le bas entretient une petite
 * valeur qui ne s'éteint jamais (un fond de -40 dB) ; vers zéro, l'énergie ne peut que décroître */
static inline int32_t sn_mq15(int32_t a, int32_t b)
{
    int32_t p = a * b;
    return p >= 0 ? p >> 15 : -((-p) >> 15);
}

static int32_t sn_rev_tick(sn_rev_t *r, int32_t x)  /* x déjà réduit (>> 4) pour rester loin des débordements */
{
    int32_t acc = 0, out, y, bo;
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        out = r->cb[c][r->idx[c]];
        r->store[c] = sn_mq15(out, SN_DAMP2_Q15) + sn_mq15(r->store[c], SN_DAMP1_Q15);
        r->cb[c][r->idx[c]] = x + sn_mq15(r->store[c], r->fb[c]);
        if (++r->idx[c] >= r->len[c]) r->idx[c] = 0;
        acc += out;
    }
    for (c = 0; c < 2u; c++) {
        bo = r->ap[c][r->aidx[c]];
        y = bo - acc;
        r->ap[c][r->aidx[c]] = acc + bo / 2;       /* (division : arrondi vers zéro) */
        if (++r->aidx[c] >= r->alen[c]) r->aidx[c] = 0;
        acc = y;
    }
    return acc;
}

/* ---------------- déroulé (n'est touché que par l'interruption audio) */
static uint32_t sn_now;                            /* échantillons depuis le démarrage (déborde en 27 h : différences signées) */
static uint8_t sn_phase, sn_muted, sn_type_cur = 255;
static uint32_t sn_wait_end, sn_end, sn_T, sn_next_ping;
static uint32_t sn_pong_at, sn_pong_pending, sn_ppong_at, sn_ppong_pending;
static uint32_t sn_cur_I = SN_I_MAX, sn_cur_E = SN_E_MAX;
static int32_t sn_mute_g = 65536, sn_mute_t = 65536, sn_mute_step;   /* Q16 */
static int32_t sn_fade_g = 65536, sn_fade_step;                      /* Q16 */
static int32_t sn_duck = 32767;
static uint32_t sn_duck_hold;
static sn_voice_t *sn_cont;                        /* le bip continu en cours, s'il y en a un */
static uint32_t sn_pings;

#define SN_AFTER(a, b) ((int32_t)((a) - (b)) >= 0)  /* a est atteint ou dépassé par rapport à b */

static uint32_t sn_interval(uint32_t r, uint32_t T)
{
    uint32_t x, i, f, p;
    if (!T || r >= T) x = 65536u;
    else {
        while (T > 65535u) { T >>= 1; r >>= 1; }
        x = T ? (r << 16) / T : 65536u;
    }
    i = x >> 8; f = x & 255u;
    p = i >= 256u ? SN_POW07[256] : SN_POW07[i] + (((SN_POW07[i + 1u] - SN_POW07[i]) * f) >> 8);
    return SN_I_MIN + (uint32_t)(((uint64_t)(SN_I_MAX - SN_I_MIN) * p) >> 16);
}
static uint32_t sn_echo(uint32_t I)
{
    uint32_t e = (uint32_t)(((uint64_t)I * SN_E_RATIO_Q16) >> 16);
    return e > SN_E_MAX ? SN_E_MAX : e;
}

static sn_voice_t *sn_alloc(void)
{
    uint32_t i, best = 0, best_age = 0xFFFFFFFFu;
    for (i = 0; i < SN_NV; i++) {
        if (!sn_v[i].on) return &sn_v[i];
        if (sn_v[i].kind != SN_CONTV && sn_v[i].age < best_age) { best_age = sn_v[i].age; best = i; }
    }
    return &sn_v[best];                            /* toutes prises : la plus ancienne (jamais un bip continu) */
}

static sn_voice_t *sn_voice(uint32_t bus, uint32_t kind)
{
    const sn_type_t *ty = &SN_TYPES[sn_cfg.type < SN_NTYPES ? sn_cfg.type : 1u];
    int32_t tone = sn_cfg.tone;
    uint32_t mul = SN_SEMI[(tone < -24 ? -24 : tone > 24 ? 24 : tone) + 24], i;
    sn_voice_t *v = sn_alloc();
    v->on = 1; v->bus = (uint8_t)bus; v->kind = (uint8_t)kind; v->np = ty->np; v->stage = 0;
    for (i = 0; i < 4u; i++) {
        v->ph[i] = 0;
        v->inc[i] = (uint32_t)(((uint64_t)ty->inc[i] * mul) >> 16);
        v->g[i] = ty->gain[i];
    }
    v->env = 0;
    v->click_env = 0; v->click_lvl = 0; v->lo = 0; v->hp = 0; v->hold = 0;
    v->age = ++sn_age;
    if (kind == SN_PING) {
        v->att = ty->attack_step; v->k = ty->decay_q30; v->lvl = SN_VOICE_Q15;
        if (ty->click_q15) { v->click_env = 32767; v->click_lvl = ty->click_q15; }
    } else if (kind == SN_PONG) {
        v->att = SN_PONG_ATTACK_STEP; v->k = SN_PONG_DECAY_Q30;
        v->lvl = (SN_VOICE_Q15 * ty->pong_q15) >> 15;
    } else {
        v->att = SN_CONT_ATTACK_STEP; v->k = (uint32_t)SN_ONE_Q30; v->lvl = SN_CONT_Q15;
    }
    return v;
}

static void sn_release(sn_voice_t *v)
{
    if (!v || !v->on) return;
    v->stage = 2; v->k = SN_CONT_RELEASE_Q30;
}

static void sn_apply_type(void)
{
    const sn_type_t *ty = &SN_TYPES[sn_cfg.type < SN_NTYPES ? sn_cfg.type : 1u];
    uint32_t c;
    for (c = 0; c < 4u; c++) { sn_rev_main.fb[c] = ty->comb_fb[c]; sn_rev_prev.fb[c] = ty->comb_fb[c]; }
    sn_type_cur = sn_cfg.type;
}

static void sn_init(void)
{
    uint32_t i;
    for (i = 0; i < SN_NV; i++) sn_v[i].on = 0;
    sn_rev_init(&sn_rev_main, sn_rev_mem[0], SN_TYPES[1].comb_fb);
    sn_rev_init(&sn_rev_prev, sn_rev_mem[1], SN_TYPES[1].comb_fb);
    sn_rev_init(&sn_rev_gen, sn_rev_mem[2], SN_GEN_COMB_FB);
    sn_phase = SN_READY; sn_muted = 0; sn_cont = 0;
    sn_apply_type();
}

static void sn_ramp_mute(int32_t target, uint32_t samples)
{
    sn_mute_t = target;
    if (!samples) samples = 1u;
    sn_mute_step = (int32_t)(65536u / samples);
    if (!sn_mute_step) sn_mute_step = 1;
}

static void sn_stop_main(void)
{
    uint32_t i;
    for (i = 0; i < SN_NV; i++)
        if (sn_v[i].on && sn_v[i].bus == 0u) sn_release(&sn_v[i]);
    sn_cont = 0;
    sn_pong_pending = 0;
}

static void sn_do_cmd(uint32_t cmd, int32_t arg)
{
    uint32_t now = sn_now;
    switch (cmd) {
    case SN_CMD_LAUNCH:
        if (sn_phase != SN_READY) break;
        sn_wait_end = now + (sn_cfg.delay_s ? sn_cfg.delay_s * SN_FS : SN_FS / 10u);
        sn_phase = SN_WAIT; sn_muted = 0;
        sn_mute_g = sn_mute_t = 65536; sn_fade_g = 0; sn_fade_step = 0;
        break;
    case SN_CMD_STOP:
        if (sn_phase == SN_READY) break;
        sn_stop_main();
        sn_phase = SN_READY; sn_muted = 0;
        sn_ramp_mute(0, SN_FS * 3u / 100u);         /* les voix s'éteignent sans bruit ; LAUNCH remet la voie à 1 */
        break;
    case SN_CMD_MUTE:
        if (sn_phase == SN_READY || sn_muted) break;
        sn_muted = 1; sn_ramp_mute(0, SN_FS * 3u / 100u);           /* 30 ms */
        break;
    case SN_CMD_UNMUTE:
        if (!sn_muted) break;
        sn_muted = 0; sn_ramp_mute(65536, SN_FS / 100u);            /* 10 ms */
        break;
    case SN_CMD_UNMUTE_FADE:
        if (!sn_muted) break;
        sn_muted = 0; sn_ramp_mute(65536, sn_cfg.fade_s ? sn_cfg.fade_s * SN_FS : SN_FS / 20u);
        break;
    case SN_CMD_WAIT_LEFT:
        if (sn_phase == SN_WAIT && arg >= 0) sn_wait_end = now + (arg ? (uint32_t)arg * SN_FS : SN_FS / 10u);
        break;
    case SN_CMD_REMAINING:
        if ((sn_phase != SN_SONAR && sn_phase != SN_CONT) || arg < 0) break;
        if ((uint32_t)arg > SN_MAX_SECONDS) arg = (int32_t)SN_MAX_SECONDS;
        sn_end = now + (uint32_t)arg * SN_FS;
        if (sn_phase == SN_SONAR && arg > 0) {    /* le rythme suit tout de suite le nouveau temps restant */
            uint32_t I = sn_interval((uint32_t)arg * SN_FS, sn_T);
            if ((int32_t)(sn_next_ping - now) > (int32_t)I) sn_next_ping = now + I;
            sn_cur_I = I; sn_cur_E = sn_echo(I);
        }
        if (sn_phase == SN_CONT && arg > 0) {      /* du temps ajouté pendant le bip continu : on repart */
            sn_release(sn_cont); sn_cont = 0;
            sn_phase = SN_SONAR;
            sn_next_ping = now + SN_FS / 10u;
        }
        break;
    case SN_CMD_PREVIEW: {
        uint32_t t = now;
        if (sn_phase == SN_CONT) {                 /* au rythme actuel : un bout de bip continu */
            sn_voice_t *v = sn_voice(1u, SN_CONTV);
            v->hold = SN_FS * 12u / 10u;
        } else {
            uint32_t E = sn_phase == SN_SONAR ? sn_cur_E : sn_echo(SN_I_MAX);
            sn_voice(1u, SN_PING);
            sn_ppong_at = t + E; sn_ppong_pending = 1;
        }
        break;
    }
    }
}

static void sn_step_timeline(void)                 /* une fois par échantillon */
{
    uint32_t now = sn_now;
    if (sn_phase == SN_WAIT && SN_AFTER(now, sn_wait_end)) {
        uint32_t fade = sn_cfg.fade_s * SN_FS;
        sn_phase = SN_SONAR;
        sn_T = sn_cfg.duration_s * SN_FS;
        sn_end = now + sn_T;
        sn_next_ping = now;
        sn_fade_g = 0;
        sn_fade_step = fade ? (int32_t)(65536u / fade) : 65536;
        if (!sn_fade_step) sn_fade_step = 1;
    }
    if (sn_phase == SN_SONAR) {
        if (SN_AFTER(now, sn_end)) {
            sn_phase = SN_CONT;
            sn_cont = sn_voice(0u, SN_CONTV);
        } else if (SN_AFTER(now, sn_next_ping)) {
            uint32_t r = sn_end - now;
            sn_cur_I = sn_interval(r, sn_T);
            sn_cur_E = sn_echo(sn_cur_I);
            sn_voice(0u, SN_PING);
            sn_pings++;
            if (sn_cur_E < r) { sn_pong_at = now + sn_cur_E; sn_pong_pending = 1; }
            sn_next_ping = now + sn_cur_I;
        }
    }
    if (sn_pong_pending && SN_AFTER(now, sn_pong_at)) {
        sn_pong_pending = 0;
        if (sn_phase == SN_SONAR) { sn_voice(0u, SN_PONG); sn_duck_hold = SN_FS * 3u / 10u; }
    }
    if (sn_ppong_pending && SN_AFTER(now, sn_ppong_at)) {
        sn_ppong_pending = 0;
        sn_voice(1u, SN_PONG);
        sn_duck_hold = SN_FS * 3u / 10u;
    }
}

static void sn_render(int32_t *out, uint32_t n)
{
    uint32_t i, j, vol, gen_wet;
    int32_t vol2;
    while (sn_q_r != sn_q_w) {                     /* les commandes de la boucle principale */
        uint32_t r = sn_q_r;
        sn_do_cmd(sn_q_cmd[r % SN_QN], sn_q_arg[r % SN_QN]);
        sn_q_r = r + 1u;
    }
    if (sn_type_cur != sn_cfg.type) sn_apply_type();
    if (sn_cont) {                                 /* la tonalité peut changer pendant le bip continu */
        const sn_type_t *ty = &SN_TYPES[sn_type_cur < SN_NTYPES ? sn_type_cur : 1u];
        uint32_t mul = SN_SEMI[sn_cfg.tone + 24];
        for (j = 0; j < 4u; j++) sn_cont->inc[j] = (uint32_t)(((uint64_t)ty->inc[j] * mul) >> 16);
    }
    vol = sn_cfg.volume;
    vol2 = (int32_t)(vol * vol * 32767u / 10000u);
    gen_wet = (uint32_t)SN_GEN_NORM_Q15 * sn_cfg.reverb / 100u;

    for (i = 0; i < n; i++) {
        int32_t dry0 = 0, dry1 = 0, send0 = 0, send1 = 0, m, p, mix, g;
        sn_step_timeline();
        for (j = 0; j < SN_NV; j++) {
            sn_voice_t *v = &sn_v[j];
            int32_t s = 0, y;
            uint32_t k;
            if (!v->on) continue;
            for (k = 0; k < v->np; k++) { v->ph[k] += v->inc[k]; s += (sn_sine(v->ph[k]) * v->g[k]) >> 15; }
            if (v->stage == 0) {
                v->env += (int32_t)v->att;
                if (v->env >= SN_ONE_Q30) { v->env = SN_ONE_Q30; v->stage = 1; }
            } else {
                v->env = (int32_t)(((int64_t)v->env * v->k) >> 30);
            }
            if (v->hold && !--v->hold) sn_release(v);
            y = (int32_t)(((int64_t)s * v->env) >> 30);
            y = (y * v->lvl) >> 15;
            if (v->click_env) {                    /* bruit filtré 500..4000 Hz, décroissance 15 ms */
                int32_t nz, bp;
                sn_noise = sn_noise * 1664525u + 1013904223u;
                nz = (int32_t)(sn_noise >> 16) - 32768;
                v->lo += ((nz - v->lo) * SN_CLICK_LP_Q15) >> 15;
                v->hp += ((nz - v->hp) * SN_CLICK_HP_Q15) >> 15;
                bp = v->lo - v->hp;
                y += (((bp * v->click_env) >> 15) * ((v->click_lvl * SN_VOICE_Q15) >> 15)) >> 13;
                v->click_env = (v->click_env * SN_CLICK_DECAY_Q15) >> 15;
                if (v->click_env < 8) v->click_env = 0;
            }
            if (v->bus) { dry1 += y; if (v->kind != SN_PONG) send1 += y; }
            else { dry0 += y; if (v->kind != SN_PONG) send0 += y; }
            if (v->stage && v->env < (1 << 14) && !v->click_env && v->kind != SN_CONTV) v->on = 0;
            if (v->stage == 2 && v->env < (1 << 14)) { v->on = 0; if (v == sn_cont) sn_cont = 0; }
        }
        /* ducking : la réverb du ping baisse de 7 dB pendant le pong */
        if (sn_duck_hold) sn_duck_hold--;
        sn_duck += (((sn_duck_hold ? SN_DUCK_Q15 : 32767) - sn_duck) >> 8);
        {
            const sn_type_t *ty = &SN_TYPES[sn_type_cur < SN_NTYPES ? sn_type_cur : 1u];
            int32_t r0 = sn_rev_tick(&sn_rev_main, send0 >> 4);
            int32_t r1 = sn_rev_tick(&sn_rev_prev, send1 >> 4);
            r0 = (int32_t)(((int64_t)r0 * ty->wet_q15) >> 11);
            r1 = (int32_t)(((int64_t)r1 * ty->wet_q15) >> 11);
            m = dry0 + ((r0 * sn_duck) >> 15);
            p = dry1 + ((r1 * sn_duck) >> 15);
        }
        /* fondu d'entrée et coupure du son : seulement la voie principale */
        if (sn_fade_g < 65536) { sn_fade_g += sn_fade_step; if (sn_fade_g > 65536) sn_fade_g = 65536; }
        if (sn_mute_g < sn_mute_t) { sn_mute_g += sn_mute_step; if (sn_mute_g > sn_mute_t) sn_mute_g = sn_mute_t; }
        else if (sn_mute_g > sn_mute_t) { sn_mute_g -= sn_mute_step; if (sn_mute_g < sn_mute_t) sn_mute_g = sn_mute_t; }
        g = (int32_t)(((int64_t)sn_fade_g * sn_mute_g) >> 16);   /* Q16 */
        m = (int32_t)(((int64_t)m * g) >> 16);
        mix = m + p;
        mix += (int32_t)(((int64_t)sn_rev_tick(&sn_rev_gen, mix >> 4) * gen_wet) >> 11);   /* réverb générale */
        mix = (int32_t)(((int64_t)mix * vol2) >> 15);
        out[2u * i] += mix;
        out[2u * i + 1u] += mix;
        sn_now++;
    }

    /* l'état, pour la boucle principale */
    {
        uint32_t now = sn_now;
        sn_stat.phase = sn_phase;
        sn_stat.muted = sn_muted;
        sn_stat.wait_left_s = sn_phase == SN_WAIT ? (int32_t)((sn_wait_end - now + SN_FS - 1u) / SN_FS) : 0;
        if (sn_phase == SN_SONAR) sn_stat.remaining_s = (int32_t)((sn_end - now + SN_FS - 1u) / SN_FS);
        else if (sn_phase == SN_CONT) sn_stat.remaining_s = -(int32_t)((now - sn_end) / SN_FS);
        else sn_stat.remaining_s = (int32_t)sn_cfg.duration_s;
        sn_stat.interval_ms = sn_cur_I * 10u / 441u;
        sn_stat.echo_ms = sn_cur_E * 10u / 441u;
        sn_stat.pings = sn_pings;
    }
}
