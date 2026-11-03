/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* L'interface du sonar : voir sonar_ui.h. Inclus après sonar_core.c (compilation unitaire). */
#include "sonar_ui.h"
#include "sonar_fonts.h"

#ifndef SN_POOL
#define SN_POOL
#endif

/* ---------------- couleurs (la maquette « Les Chants de Tindalos ») */
#define UI_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_ABYSSE UI_RGB(0x0A, 0x12, 0x14)
#define C_ENCRE UI_RGB(0x14, 0x23, 0x26)
#define C_TRAIT UI_RGB(0x1E, 0x32, 0x36)
#define C_OS UI_RGB(0xEF, 0xE6, 0xD2)
#define C_BRUME UI_RGB(0x9D, 0xB3, 0xAC)
#define C_LAITON UI_RGB(0xD4, 0xAF, 0x6A)
#define C_VDG UI_RGB(0x5F, 0xC4, 0xA8)
#define C_SANG UI_RGB(0xFF, 0x6B, 0x57)
#define C_WAIT_BG UI_RGB(0x2B, 0x24, 0x14)
#define C_SONAR_BG UI_RGB(0x10, 0x30, 0x2A)
#define C_FINAL_BG UI_RGB(0x3A, 0x15, 0x12)
#define C_CONT_BG UI_RGB(0x1F, 0x0B, 0x09)

/* ---------------- presets et réglages gardés en flash */
#define UI_NPRESET 32u
#define UI_NAME 20u
#define UI_VERSION 0x534E3031u          /* "SN01" */
typedef struct { char name[UI_NAME]; sn_settings_t s; } ui_preset_t;
typedef struct { uint32_t version; uint32_t n; ui_preset_t p[UI_NPRESET]; } ui_presets_t;
typedef struct { uint32_t version; sn_settings_t s; int32_t current; } ui_saved_t;

static const sn_settings_t UI_DEFAULT = {0, 20, 3600, 70, 30, 1, 0};
static ui_presets_t ui_pre;
static sn_settings_t ui_s;
static int32_t ui_cur;                  /* preset affiché ; ui_pre.n = « + Nouveau » */
static uint32_t ui_ms, ui_dirty_s, ui_dirty_p, ui_changed_ms, ui_preview_ms;
static uint32_t ui_play_down, ui_rec_down, ui_rec_fired;
static uint32_t ui_focus, ui_focus_until, ui_presets_until, ui_saved_until, ui_last_ping, ui_ping_ms;
static int32_t ui_tod_ms;               /* heure du jour en ms à ui_ms = 0 (modulo 24 h) */
static uint8_t ui_tod_ok;               /* l'heure a été reçue du compagnon */
static uint8_t ui_tod_h, ui_tod_m, ui_tod_s, ui_name_idx, ui_name_len;
/* presets envoyés par CDT Compagnon (la page est la référence) : reçus à part, appliqués d'un bloc si tout est arrivé */
static ui_presets_t ui_rx SN_POOL;
static uint32_t ui_rx_on, ui_rx_count, ui_rx_seen, ui_rx_val, ui_rx_sum, ui_synced_until;
static sn_settings_t ui_rx_s;
static char ui_name_buf[UI_NAME];
static sn_status_t ui_st;

enum { F_NONE = 0, F_DELAY, F_FADE, F_DURATION, F_VOLUME, F_REVERB, F_TONE };

static void ui_copy_name(char *dst, const char *src)
{
    uint32_t i;
    for (i = 0; i + 1u < UI_NAME && src[i]; i++) dst[i] = src[i];
    for (; i < UI_NAME; i++) dst[i] = 0;
}

static void ui_new_name(char *dst, uint32_t n)       /* "Preset 3" */
{
    char s[UI_NAME] = "Preset ";
    uint32_t i = 7;
    if (n >= 10u) s[i++] = (char)('0' + n / 10u % 10u);
    s[i++] = (char)('0' + n % 10u);
    s[i] = 0;
    ui_copy_name(dst, s);
}

/* le volume se règle au MASTER : le sonar garde un niveau fixe (celui des premiers essais) */
#define UI_VOLUME 70u
static void ui_apply(void) { ui_s.volume = UI_VOLUME; sn_set(&ui_s); }

static void ui_load_preset(int32_t i)
{
    if (i >= 0 && (uint32_t)i < ui_pre.n) { ui_s = ui_pre.p[i].s; ui_apply(); }
}

static void ui_init(uint32_t ms)
{
    ui_saved_t sv;
    ui_ms = ms;
    if (ui_hw_load(1u, &ui_pre, sizeof ui_pre) || ui_pre.version != UI_VERSION || !ui_pre.n || ui_pre.n > UI_NPRESET) {
        ui_pre.version = UI_VERSION;
        ui_pre.n = 1u;
        ui_copy_name(ui_pre.p[0].name, "Spring's Island");
        ui_pre.p[0].s = UI_DEFAULT;
    }
    if (!ui_hw_load(0u, &sv, sizeof sv) && sv.version == UI_VERSION) {
        ui_s = sv.s;
        ui_cur = sv.current >= 0 && (uint32_t)sv.current <= ui_pre.n ? sv.current : 0;
    } else {
        ui_s = ui_pre.p[0].s;
        ui_cur = 0;
    }
    ui_apply();
}

static int ui_can_save_now(void)
{
    return ui_st.phase == SN_READY && ui_ms - ui_changed_ms > 2000u && ui_ms - ui_preview_ms > 4000u;
}

/* ---------------- les réglages, cran par cran (les mêmes pas que la page web) */
static uint32_t ui_step_value(uint32_t f, int32_t dir, uint32_t v)
{
    int32_t d, x = (int32_t)v;
    switch (f) {
    case F_DELAY: d = dir > 0 ? (v < 120u ? 5 : 60) : (v > 120u ? 60 : 5); x += dir * d; break;
    case F_FADE: x += dir * 5; break;
    case F_DURATION: d = dir > 0 ? (v < 300u ? 10 : 60) : (v > 300u ? 60 : 10); x += dir * d; break;
    case F_VOLUME: case F_REVERB: x += dir * 2; if (x > 100) x = 100; break;
    }
    if (x < 0) x = 0;
    if (f != F_VOLUME && f != F_REVERB && x > (int32_t)SN_MAX_SECONDS) x = (int32_t)SN_MAX_SECONDS;
    return (uint32_t)x;
}

static void ui_touch(uint32_t f)
{
    ui_focus = f;
    ui_focus_until = ui_ms + 1600u;
    ui_changed_ms = ui_ms;
}

static void ui_turn(uint32_t f, int32_t det)
{
    int32_t dir = det > 0 ? 1 : -1, n = det > 0 ? det : -det;
    uint8_t ph = ui_st.phase;
    if (!det) return;
    if (f == F_DELAY && ph != SN_READY) {          /* pendant l'attente : le temps avant le départ */
        uint32_t left = (uint32_t)(ui_st.wait_left_s > 0 ? ui_st.wait_left_s : 0);
        if (ph != SN_WAIT) return;
        while (n--) left = ui_step_value(F_DELAY, dir, left);
        ui_st.wait_left_s = (int32_t)left;
        sn_cmd(SN_CMD_WAIT_LEFT, (int32_t)left);   /* une seule commande par mouvement */
    } else if (f == F_DURATION && (ph == SN_SONAR || ph == SN_CONT)) {   /* en jeu : le temps restant */
        int32_t r = ui_st.remaining_s;
        uint32_t nr;
        if (r <= 0 && dir < 0) return;            /* jamais sous zéro */
        nr = r <= 0 ? 60u : (uint32_t)r;
        if (r <= 0) n--;                           /* le premier cran en dépassement repart à 1 min */
        while (n-- > 0) nr = ui_step_value(F_DURATION, dir, nr);
        ui_st.remaining_s = (int32_t)nr;
        if (ph == SN_CONT && nr > 0u) ui_st.phase = SN_SONAR;
        sn_cmd(SN_CMD_REMAINING, (int32_t)nr);
    } else {
        while (n--) {
            switch (f) {
            case F_DELAY: ui_s.delay_s = ui_step_value(f, dir, ui_s.delay_s); break;
            case F_FADE: ui_s.fade_s = ui_step_value(f, dir, ui_s.fade_s); break;
            case F_DURATION: ui_s.duration_s = ui_step_value(f, dir, ui_s.duration_s); break;
            case F_VOLUME: ui_s.volume = (uint8_t)ui_step_value(f, dir, ui_s.volume); break;
            case F_REVERB: ui_s.reverb = (uint8_t)ui_step_value(f, dir, ui_s.reverb); break;
            }
        }
        ui_apply();
        ui_dirty_s = 1;
    }
    ui_touch(f);
}

static int ui_preview_ok(void)
{
    return ui_st.phase == SN_READY || ui_st.phase == SN_WAIT || ui_st.muted;
}

static void ui_step(const ui_in_t *in)
{
    uint32_t b = in->pressed, r = in->released;
    int32_t e;
    ui_ms = in->ms;
    sn_status(&ui_st);

    /* les quatre molettes de droite (le volume : MASTER), ALGORITHM (réverbération aussi), SELECT */
    ui_turn(F_DELAY, in->enc[UI_ENC_K1]);
    ui_turn(F_FADE, in->enc[UI_ENC_K2]);
    ui_turn(F_REVERB, in->enc[UI_ENC_K3]);
    ui_turn(F_DURATION, in->enc[UI_ENC_K4]);
    ui_turn(F_REVERB, in->enc[UI_ENC_ALGORITHM]);
    if ((e = in->enc[UI_ENC_SELECT]) != 0) {            /* type de son : son nom dans la barre du milieu */
        int32_t t = ((int32_t)ui_s.type + e) % (int32_t)SN_NTYPES;
        ui_s.type = (uint8_t)(t < 0 ? t + (int32_t)SN_NTYPES : t);
        ui_apply(); ui_dirty_s = 1; ui_changed_ms = ui_ms;
    }

    /* PRESETS : faire défiler (au repos seulement), puis « + Nouveau » */
    if ((e = in->enc[UI_ENC_PRESETS]) != 0 && ui_st.phase == SN_READY) {
        int32_t c = ui_cur + e;
        if (c < 0) c = 0;
        if (c > (int32_t)ui_pre.n) c = (int32_t)ui_pre.n;
        if (c != ui_cur) { ui_cur = c; ui_load_preset(c); ui_dirty_s = 1; ui_changed_ms = ui_ms; }
        ui_presets_until = ui_ms + 3000u;
    }

    /* OCT- / OCT+ : la tonalité, un demi-ton par appui */
    if (b & (1u << UI_BTN_OCT_DOWN) && ui_s.tone > -24) { ui_s.tone--; ui_apply(); ui_dirty_s = 1; ui_touch(F_TONE); }
    if (b & (1u << UI_BTN_OCT_UP) && ui_s.tone < 24) { ui_s.tone++; ui_apply(); ui_dirty_s = 1; ui_touch(F_TONE); }

    /* SAVE : dans le preset affiché, ou un nouveau sur « + Nouveau » */
    if (b & (1u << UI_BTN_SAVE)) {
        if (ui_cur >= (int32_t)ui_pre.n && ui_pre.n < UI_NPRESET) {
            ui_new_name(ui_pre.p[ui_pre.n].name, ui_pre.n + 1u);
            ui_pre.p[ui_pre.n].s = ui_s;
            ui_cur = (int32_t)ui_pre.n++;
        } else if (ui_cur < (int32_t)ui_pre.n) {
            ui_pre.p[ui_cur].s = ui_s;
        }
        ui_dirty_p = 1; ui_dirty_s = 1; ui_changed_ms = ui_ms;
        ui_saved_until = ui_ms + 2000u;
    }

    /* PLAY : au repos, lancer ; ensuite couper / rétablir (court : tout de suite, long : en fondu) */
    if (b & (1u << UI_BTN_PLAY)) ui_play_down = ui_ms;
    if (r & (1u << UI_BTN_PLAY)) {
        int is_long = ui_ms - ui_play_down >= 600u;
        if (ui_st.phase == SN_READY) { ui_apply(); sn_cmd(SN_CMD_LAUNCH, 0); ui_st.phase = SN_WAIT; }
        else if (!ui_st.muted) { sn_cmd(SN_CMD_MUTE, 0); ui_st.muted = 1; }
        else { sn_cmd(is_long ? SN_CMD_UNMUTE_FADE : SN_CMD_UNMUTE, 0); ui_st.muted = 0; }
    }

    /* REC maintenu 1 s : arrêter et réinitialiser */
    if (b & (1u << UI_BTN_REC)) { ui_rec_down = ui_ms; ui_rec_fired = 0; }
    if ((in->held & (1u << UI_BTN_REC)) && !ui_rec_fired && ui_st.phase != SN_READY && ui_ms - ui_rec_down >= 1000u) {
        sn_cmd(SN_CMD_STOP, 0);
        ui_rec_fired = 1;
        ui_st.phase = SN_READY;
    }

    /* une touche du clavier : écouter (avant le départ, ou quand le son est coupé) */
    if (in->keys_pressed && ui_preview_ok()) { sn_cmd(SN_CMD_PREVIEW, 0); ui_preview_ms = ui_ms; }

    /* LED (éteinte, lueur ou allumée ; PLAY a en plus une LED verte)
     *   PLAY : verte au repos (prêt) ; en jeu, sa LED propre allumée (baisse un instant à chaque ping), clignote son coupé
     *   REC  : pulse pendant le sonar (allumée / lueur), allumée tant qu'elle est tenue
     *   OCT selon la tonalité, SAVE quand les réglages diffèrent du preset affiché */
    if (ui_st.pings != ui_last_ping) { ui_last_ping = ui_st.pings; ui_ping_ms = ui_ms; }
    ui_hw_led(UI_LED_PLAY_GREEN, ui_st.phase == SN_READY ? 2 : 0);
    if (ui_st.phase == SN_READY) ui_hw_led(UI_BTN_PLAY, 0);
    else if (ui_st.muted) ui_hw_led(UI_BTN_PLAY, (ui_ms / 500u) & 1u ? 0 : 2);
    else ui_hw_led(UI_BTN_PLAY, ui_ms - ui_ping_ms > 90u ? 2 : 1);
    if (in->held & (1u << UI_BTN_REC)) ui_hw_led(UI_BTN_REC, 2);
    else if (ui_st.phase != SN_READY) ui_hw_led(UI_BTN_REC, (ui_ms / 600u) & 1u ? 1 : 2);
    else ui_hw_led(UI_BTN_REC, 0);
    ui_hw_led(UI_BTN_OCT_DOWN, ui_s.tone < 0 ? 2 : 0);
    ui_hw_led(UI_BTN_OCT_UP, ui_s.tone > 0 ? 2 : 0);
    {
        const sn_settings_t *p = ui_cur < (int32_t)ui_pre.n ? &ui_pre.p[ui_cur].s : 0;
        int same = p && p->delay_s == ui_s.delay_s && p->fade_s == ui_s.fade_s && p->duration_s == ui_s.duration_s &&
                   p->reverb == ui_s.reverb && p->type == ui_s.type && p->tone == ui_s.tone;
        ui_hw_led(UI_BTN_SAVE, same ? 0 : 2);
    }

    /* écrire en flash seulement au repos, en silence (un effacement coupe le son 50 ms) */
    if ((ui_dirty_s || ui_dirty_p) && ui_can_save_now()) {
        if (ui_dirty_p) { ui_hw_save(1u, &ui_pre, sizeof ui_pre); ui_dirty_p = 0; }
        if (ui_dirty_s) {
            ui_saved_t sv;
            sv.version = UI_VERSION; sv.s = ui_s; sv.current = ui_cur;
            ui_hw_save(0u, &sv, sizeof sv);
            ui_dirty_s = 0;
        }
    }
}

/* heure et noms depuis le compagnon (CDT Compagnon) : contrôleurs MIDI, canal 16
 *   CC 102 heure, 103 minute, 104 seconde, 105 valider ; CC 110 numéro de preset, 111 un caractère, 112 valider */
/* MIDI de CDT Compagnon, contrôleurs sur le canal 16 :
 *   102 h, 103 min, 104 s, 105 valider                       l'heure
 *   110 n° (0 = P1), 111 un caractère ASCII..., 112 valider  un nom de preset (seul, ou dans un envoi de presets)
 *   113 nombre                                               début d'un envoi de presets (remplace tous ceux du FM-1)
 *     pour chacun : 110 n°, 111 caractères, puis par réglage 120 / 121 / 122 (7 bits chacun, poids faible d'abord)
 *     et 123 n° du réglage (1 départ, 2 fade, 3 durée, 4 réverb, 5 type, 6 tonalité + 24), enfin 112
 *   114 somme                                                fin : somme des valeurs de 110, 111, 120..123, modulo 128 */
static void ui_rx_apply(void)
{
    uint32_t i;
    ui_rx.version = UI_VERSION;
    ui_rx.n = ui_rx_count;
    ui_pre = ui_rx;
    for (i = ui_pre.n; i < UI_NPRESET; i++) { ui_pre.p[i].name[0] = 0; ui_pre.p[i].s = UI_DEFAULT; }
    if (ui_cur > (int32_t)ui_pre.n) ui_cur = (int32_t)ui_pre.n;
    if (ui_st.phase == SN_READY && ui_cur < (int32_t)ui_pre.n) ui_load_preset(ui_cur);   /* en jeu : le déroulé ne change pas */
    ui_dirty_p = 1; ui_dirty_s = 1; ui_changed_ms = ui_ms;
    ui_synced_until = ui_ms + 2500u;
}

static void ui_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (st != 0xBFu) return;
    if (ui_rx_on && ((d1 >= 110u && d1 <= 111u) || (d1 >= 120u && d1 <= 123u))) ui_rx_sum += d2;
    switch (d1) {
    case 102: ui_tod_h = d2 % 24u; break;
    case 103: ui_tod_m = d2 % 60u; break;
    case 104: ui_tod_s = d2 % 60u; break;
    case 105:
        ui_tod_ms = (int32_t)(((uint32_t)ui_tod_h * 3600u + ui_tod_m * 60u + ui_tod_s) * 1000u) - (int32_t)(ui_ms % 86400000u);
        ui_tod_ok = 1;
        break;
    case 110: ui_name_idx = d2; ui_name_len = 0; ui_rx_s = UI_DEFAULT; break;
    case 111: if (ui_name_len + 1u < UI_NAME && d2 >= 32u && d2 < 127u) ui_name_buf[ui_name_len++] = (char)d2; break;
    case 112:
        ui_name_buf[ui_name_len] = 0;
        if (ui_rx_on) {
            if (ui_name_idx < ui_rx_count && ui_name_len) {
                ui_copy_name(ui_rx.p[ui_name_idx].name, ui_name_buf);
                ui_rx.p[ui_name_idx].s = ui_rx_s;
                ui_rx_seen |= 1u << ui_name_idx;
            }
        } else if (ui_name_idx < ui_pre.n && ui_name_len) {
            ui_copy_name(ui_pre.p[ui_name_idx].name, ui_name_buf); ui_dirty_p = 1; ui_changed_ms = ui_ms;
        }
        break;
    case 113:
        ui_rx_on = d2 >= 1u && d2 <= UI_NPRESET;
        ui_rx_count = d2; ui_rx_seen = 0; ui_rx_sum = 0;
        break;
    case 120: ui_rx_val = d2; break;
    case 121: ui_rx_val |= (uint32_t)d2 << 7; break;
    case 122: ui_rx_val |= (uint32_t)d2 << 14; break;
    case 123: {
        uint32_t v = ui_rx_val;
        switch (d2) {
        case 1: ui_rx_s.delay_s = v > SN_MAX_SECONDS ? SN_MAX_SECONDS : v; break;
        case 2: ui_rx_s.fade_s = v > SN_MAX_SECONDS ? SN_MAX_SECONDS : v; break;
        case 3: ui_rx_s.duration_s = v > SN_MAX_SECONDS ? SN_MAX_SECONDS : v; break;
        case 4: ui_rx_s.reverb = (uint8_t)(v > 100u ? 100u : v); break;
        case 5: ui_rx_s.type = (uint8_t)(v < SN_NTYPES ? v : 1u); break;
        case 6: ui_rx_s.tone = (int8_t)((v > 48u ? 48 : (int32_t)v) - 24); break;
        }
        break;
    }
    case 114:
        if (ui_rx_on && (ui_rx_sum & 127u) == d2 && ui_rx_seen == (ui_rx_count >= 32u ? 0xFFFFFFFFu : (1u << ui_rx_count) - 1u))
            ui_rx_apply();
        ui_rx_on = 0;
        break;
    }
}

/* ---------------- dessin : une zone à la fois, dans un tampon, puis envoyée à l'écran */
static uint16_t ui_buf[240u * 120u] SN_POOL;
static uint32_t ui_rw, ui_rh;

static void ui_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i;
    ui_hw_sync();
    ui_rw = w; ui_rh = h;
    for (i = 0; i < w * h; i++) ui_buf[i] = bg;
}
static void ui_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < (int32_t)ui_rh; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < (int32_t)ui_rw; i++)
            ui_buf[j * (int32_t)ui_rw + i] = c;
}
static uint16_t ui_mix(uint16_t bg, uint16_t fg, uint32_t a)   /* a : 0..15 */
{
    int32_t r0 = bg >> 11, g0 = (bg >> 5) & 63, b0 = bg & 31;
    int32_t r1 = fg >> 11, g1 = (fg >> 5) & 63, b1 = fg & 31;
    int32_t r = r0 + (((r1 - r0) * (int32_t)a) / 15), g = g0 + (((g1 - g0) * (int32_t)a) / 15), b = b0 + (((b1 - b0) * (int32_t)a) / 15);
    return (uint16_t)(r << 11 | g << 5 | b);
}
static void ui_px(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *p;
    if (x < 0 || y < 0 || x >= (int32_t)ui_rw || y >= (int32_t)ui_rh || !a) return;
    p = &ui_buf[y * (int32_t)ui_rw + x];
    *p = a >= 15u ? c : ui_mix(*p, c, a);
}
static void ui_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        ui_px(cx + x, cy + y, c, 15); ui_px(cx + y, cy + x, c, 15); ui_px(cx - y, cy + x, c, 15); ui_px(cx - x, cy + y, c, 15);
        ui_px(cx - x, cy - y, c, 15); ui_px(cx - y, cy - x, c, 15); ui_px(cx + y, cy - x, c, 15); ui_px(cx + x, cy - y, c, 15);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

static uint32_t ui_utf8(const char **s)               /* le caractère suivant d'une chaîne UTF-8 */
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xE0u && p[0] && p[1]) { c = (c & 15u) << 12 | (uint32_t)(p[0] & 63u) << 6 | (p[1] & 63u); p += 2; }
    else if (c >= 0xC0u && p[0]) { c = (c & 31u) << 6 | (p[0] & 63u); p += 1; }
    *s = (const char *)p;
    return c;
}
static const sn_glyph_t *ui_glyph(const sn_font_t *f, uint32_t cp)
{
    int32_t lo = 0, hi = (int32_t)f->n - 1;
    while (lo <= hi) {
        int32_t m = (lo + hi) / 2;
        if (f->g[m].cp == cp) return &f->g[m];
        if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return cp != '?' ? ui_glyph(f, '?') : 0;
}
static int32_t ui_text_w(const sn_font_t *f, const char *s, int32_t track)
{
    int32_t w = 0;
    while (*s) {
        const sn_glyph_t *g = ui_glyph(f, ui_utf8(&s));
        if (g) w += g->adv + track;
    }
    return w > 0 ? w - track : 0;
}
static int32_t ui_text(const sn_font_t *f, int32_t x, int32_t base, const char *s, uint16_t c, int32_t track)
{
    while (*s) {
        const sn_glyph_t *g = ui_glyph(f, ui_utf8(&s));
        uint32_t i, j, stride;
        const uint8_t *d;
        if (!g) continue;
        stride = (g->w + 1u) / 2u;
        d = f->data + g->off;
        for (j = 0; j < g->h; j++)
            for (i = 0; i < g->w; i++) {
                uint32_t v = d[j * stride + i / 2u];
                ui_px(x + g->x + (int32_t)i, base + g->y + (int32_t)j, c, i & 1u ? v & 15u : v >> 4);
            }
        x += g->adv + track;
    }
    return x;
}
static void ui_text_c(const sn_font_t *f, int32_t cx, int32_t base, const char *s, uint16_t c)
{
    ui_text(f, cx - ui_text_w(f, s, 0) / 2, base, s, c, 0);
}
static void ui_send(uint32_t x, uint32_t y) { ui_hw_blit(x, y, ui_rw, ui_rh, ui_buf); }

/* ---------------- textes */
static char *ui_cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *ui_num(char *p, uint32_t v)
{
    char t[12];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static void ui_mmss(char *p, uint32_t s)                /* 125:00, 0:30 */
{
    p = ui_num(p, s / 60u);
    *p++ = ':';
    *p++ = (char)('0' + s % 60u / 10u);
    *p++ = (char)('0' + s % 10u);
    *p = 0;
}
static uint32_t ui_tod_now_s(void)                      /* secondes depuis minuit (l'heure synchronisée) */
{
    int32_t v = ui_tod_ms + (int32_t)(ui_ms % 86400000u);
    while (v < 0) v += 86400000;
    while (v >= 86400000) v -= 86400000;
    return (uint32_t)v / 1000u;
}
static int ui_clock(char *p, uint32_t ahead_s)          /* l'heure dans ahead_s secondes : "21:47" ; 0 si inconnue */
{
    uint32_t t, h, m;
    if (!ui_tod_ok) return 0;
    t = ui_tod_now_s() + ahead_s;
    h = t / 3600u % 24u; m = t / 60u % 60u;
    p[0] = (char)('0' + h / 10u); p[1] = (char)('0' + h % 10u); p[2] = ':';
    p[3] = (char)('0' + m / 10u); p[4] = (char)('0' + m % 10u); p[5] = 0;
    return 1;
}
static void ui_pct(char *p, uint32_t v) { p = ui_num(p, v); ui_cat(p, " %"); }
static void ui_tone(char *p, int32_t t)
{
    if (t > 0) *p++ = '+';
    if (t < 0) { p = ui_cat(p, "−"); t = -t; }
    ui_num(p, (uint32_t)t);
}

/* ---------------- ce qui est à l'écran (redessiné seulement s'il change) */
typedef struct {
    char name[UI_NAME + 4], pill[16];
    uint16_t pill_fg, pill_bg;
} ui_head_t;
typedef struct {
    char label[24], big[16], title1[UI_NAME], title2[UI_NAME], sub[40], hint[40];
    uint16_t big_c, sub_c, bg, ring;
} ui_mid_t;
typedef struct { char p[8], son[UI_NAME], ton[8]; } ui_meta_t;
typedef struct { char lab[4][10], val[4][12]; uint16_t vc[4]; uint8_t active; } ui_strip_t;
static ui_head_t ui_dh;
static ui_mid_t ui_dm;
static ui_meta_t ui_dt;
static ui_strip_t ui_ds;
static uint32_t ui_drawn;                               /* 0 : tout redessiner */

static void ui_draw_head(const ui_head_t *h)
{
    int32_t w = ui_text_w(&SN_F_LAB, h->pill, 1) + 14;
    ui_begin(240, 30, C_ABYSSE);
    ui_text(&SN_F_HDR, 10, 20, h->name, C_LAITON, 1);
    ui_fill(230 - w, 6, w, 18, h->pill_bg);
    ui_text(&SN_F_LAB, 237 - w, 19, h->pill, h->pill_fg, 1);
    ui_fill(0, 29, 240, 1, C_TRAIT);
    ui_send(0, 0);
}
static void ui_draw_mid(const ui_mid_t *m)
{
    int has_hint = m->hint[0] != 0, has_title = m->title1[0] != 0;
    ui_begin(240, 120, m->bg);
    if (m->ring) {                                       /* les cercles du radar, fixes (pas d'animation : la batterie) */
        ui_circle(120, 62, 26, ui_mix(m->bg, m->ring, 4));
        ui_circle(120, 62, 48, ui_mix(m->bg, m->ring, 3));
        ui_circle(120, 62, 70, ui_mix(m->bg, m->ring, 2));
        ui_circle(120, 62, 92, ui_mix(m->bg, m->ring, 1));
    }
    if (has_title) {
        ui_text_c(&SN_F_LAB14, 120, 24, m->label, C_BRUME);
        if (m->title2[0]) { ui_text_c(&SN_F_TITLE, 120, 58, m->title1, C_LAITON); ui_text_c(&SN_F_TITLE, 120, 86, m->title2, C_LAITON); }
        else ui_text_c(&SN_F_TITLE, 120, 70, m->title1, C_LAITON);
        ui_text_c(&SN_F_SUB, 120, 110, m->sub, m->sub_c);
    } else {
        int32_t y0 = has_hint ? -9 : 0;
        ui_text_c(&SN_F_LAB14, 120, 22 + y0, m->label, C_BRUME);
        ui_text_c(&SN_F_BIG, 120, 88 + y0, m->big, m->big_c);
        ui_text_c(&SN_F_SUB, 120, 106 + y0, m->sub, m->sub_c);
        if (has_hint) ui_text_c(&SN_F_SUB, 120, 115, m->hint, C_OS);
    }
    ui_send(0, 30);
}
static void ui_draw_meta(const ui_meta_t *t)
{
    int32_t x;
    ui_begin(240, 22, C_ABYSSE);
    ui_text(&SN_F_LAB14, 10, 16, t->p, C_BRUME, 0);
    ui_text_c(&SN_F_LAB14, 120, 16, t->son, C_OS);    /* le type de son (SELECT) */
    x = 230 - ui_text_w(&SN_F_LAB14, "TON ", 0) - ui_text_w(&SN_F_LAB14, t->ton, 0);
    x = ui_text(&SN_F_LAB14, x, 16, "TON ", C_BRUME, 0);
    ui_text(&SN_F_LAB14, x, 16, t->ton, C_OS, 0);
    ui_send(0, 150);
}
static void ui_draw_strip(const ui_strip_t *s)
{
    uint32_t i;
    ui_begin(240, 68, C_ABYSSE);
    for (i = 0; i < 4u; i++) {
        int32_t x = 6 + (int32_t)i * 58;
        uint16_t border = s->active == i + 1u ? C_VDG : C_ENCRE;
        ui_fill(x, 0, 54, 62, border);
        ui_fill(x + 2, 2, 50, 58, C_ENCRE);
        ui_text_c(&SN_F_LAB, x + 27, 22, s->lab[i], C_BRUME);
        ui_text_c(&SN_F_VAL, x + 27, 48, s->val[i], s->active == i + 1u ? C_VDG : s->vc[i]);
    }
    ui_send(0, 172);
}

static void ui_draw(void)
{
    ui_head_t h;
    ui_mid_t m;
    ui_meta_t t;
    ui_strip_t s;
    uint8_t ph = ui_st.phase;
    int focus = ui_focus && (int32_t)(ui_focus_until - ui_ms) > 0;
    int presets = ph == SN_READY && (int32_t)(ui_presets_until - ui_ms) > 0;
    int32_t r = ui_st.remaining_s;
    char tmp[16];
    uint32_t i;

    /* en-tête */
    for (i = 0; i < sizeof h; i++) ((char *)&h)[i] = 0;
    if (ui_cur < (int32_t)ui_pre.n) ui_copy_name(h.name, ui_pre.p[ui_cur].name);
    else ui_copy_name(h.name, "Nouveau preset");
    ui_cat(h.pill, "PRÊT"); h.pill_fg = C_OS; h.pill_bg = C_ENCRE;
    if (ph == SN_WAIT) { h.pill[0] = 0; ui_cat(h.pill, "ATTENTE"); h.pill_fg = C_LAITON; h.pill_bg = C_WAIT_BG; }
    if (ph == SN_SONAR) {
        h.pill[0] = 0;
        if (r <= 60) { ui_cat(h.pill, "FIN PROCHE"); h.pill_fg = C_SANG; h.pill_bg = C_FINAL_BG; }
        else { ui_cat(h.pill, "SONAR"); h.pill_fg = C_VDG; h.pill_bg = C_SONAR_BG; }
    }
    if (ph == SN_CONT) { h.pill[0] = 0; ui_cat(h.pill, "BIP CONTINU"); h.pill_fg = C_CONT_BG; h.pill_bg = C_SANG; }
    if (ph != SN_READY && ui_st.muted) { h.pill[0] = 0; ui_cat(h.pill, "SON COUPÉ"); h.pill_fg = C_ABYSSE; h.pill_bg = C_OS; }

    /* zone centrale */
    for (i = 0; i < sizeof m; i++) ((char *)&m)[i] = 0;
    m.bg = ph == SN_CONT ? C_CONT_BG : C_ABYSSE;
    m.big_c = C_OS; m.sub_c = C_LAITON;
    if (ph == SN_READY) {
        ui_cat(m.label, "DURÉE DU SONAR");
        ui_mmss(m.big, ui_s.duration_s);
        if (ui_clock(tmp, ui_s.delay_s + ui_s.duration_s)) { ui_cat(ui_cat(m.sub, "fin vers "), tmp); }
        else ui_cat(m.sub, "heure non synchronisée");
        if (!ui_clock(tmp, 0)) m.sub_c = C_BRUME;
    } else if (ph == SN_WAIT) {
        ui_cat(m.label, "DÉPART DANS");
        ui_mmss(m.big, (uint32_t)ui_st.wait_left_s);
        m.big_c = C_LAITON;
        ui_mmss(tmp, ui_s.duration_s);
        ui_cat(ui_cat(ui_cat(m.sub, "puis "), tmp), " de sonar");
        m.sub_c = C_BRUME;
    } else if (ph == SN_SONAR) {
        char *p;
        uint32_t iv = ui_st.interval_ms;
        ui_cat(m.label, "RESTANT");
        ui_mmss(m.big, (uint32_t)(r > 0 ? r : 0));
        m.ring = r <= 60 ? C_SANG : C_VDG;
        if (r <= 60) m.big_c = C_SANG;
        p = m.sub;
        if (ui_clock(tmp, (uint32_t)(r > 0 ? r : 0))) { p = ui_cat(ui_cat(ui_cat(p, "fin "), tmp), " · "); }
        p = ui_cat(p, "ping ");
        p = ui_num(p, iv / 1000u);
        *p++ = ',';
        *p++ = (char)('0' + iv / 100u % 10u);
        ui_cat(p, " s");
        if (r <= 60) m.sub_c = C_OS;
    } else {
        char *p = m.big;
        ui_cat(m.label, "DÉPASSEMENT");
        *p++ = '+';
        ui_mmss(p, (uint32_t)(-r));
        m.big_c = C_SANG; m.sub_c = C_OS;
        if (ui_clock(tmp, 0)) {
            char fin[8];
            ui_clock(fin, 0);
            {   /* l'heure de fin : maintenant moins le dépassement */
                uint32_t t = ui_tod_now_s() + 86400u - (uint32_t)(-r) % 86400u, hh = t / 3600u % 24u, mm = t / 60u % 60u;
                fin[0] = (char)('0' + hh / 10u); fin[1] = (char)('0' + hh % 10u); fin[2] = ':';
                fin[3] = (char)('0' + mm / 10u); fin[4] = (char)('0' + mm % 10u); fin[5] = 0;
            }
            ui_cat(ui_cat(ui_cat(ui_cat(m.sub, "fin "), fin), " · il est "), tmp);
        } else ui_cat(m.sub, "REC maintenu : arrêter");
    }
    if (ph != SN_READY && ui_st.muted) {
        m.big_c = C_BRUME; m.ring = 0;
        m.sub[0] = 0; ui_cat(m.sub, "PLAY court : son immédiat");
        ui_cat(m.hint, "PLAY long : retour en fondu");
        m.sub_c = C_LAITON;
    }
    if (focus) {
        static const char *const LAB[] = {"", "DÉPART", "FADE-IN", "DURÉE", "VOLUME", "RÉVERB", "TONALITÉ"};
        int skip = (ph == SN_WAIT && ui_focus == F_DELAY) || ((ph == SN_SONAR || ph == SN_CONT) && ui_focus == F_DURATION);
        if (!skip) {
            m.label[0] = 0; ui_cat(m.label, LAB[ui_focus]);
            switch (ui_focus) {
            case F_DELAY: ui_mmss(m.big, ui_s.delay_s); break;
            case F_FADE: ui_mmss(m.big, ui_s.fade_s); break;
            case F_DURATION: ui_mmss(m.big, ui_s.duration_s); break;
            case F_VOLUME: ui_pct(m.big, ui_s.volume); break;
            case F_REVERB: ui_pct(m.big, ui_s.reverb); break;
            case F_TONE: ui_tone(m.big, ui_s.tone); break;
            }
            m.big_c = C_VDG;
        }
    }
    if (presets) {
        uint32_t w;
        m.ring = 0; m.hint[0] = 0;
        m.label[0] = 0;
        if (ui_cur < (int32_t)ui_pre.n) {
            char *p = ui_cat(m.label, "PRESET ");
            p = ui_num(p, (uint32_t)ui_cur + 1u);
            p = ui_cat(p, " / ");
            ui_num(p, ui_pre.n);
            ui_copy_name(m.title1, ui_pre.p[ui_cur].name);
        } else {
            ui_cat(m.label, "NOUVEAU PRESET");
            ui_new_name(m.title1, ui_pre.n + 1u);
        }
        w = (uint32_t)ui_text_w(&SN_F_TITLE, m.title1, 0);
        if (w > 220u) {                                   /* trop long : sur deux lignes, au dernier espace avant le milieu */
            int32_t k, cut = -1, len = 0;
            while (m.title1[len]) len++;
            for (k = 0; k < len; k++) if (m.title1[k] == ' ' && (cut < 0 || k <= len / 2 + 2)) cut = k;
            if (cut > 0) { ui_copy_name(m.title2, m.title1 + cut + 1); m.title1[cut] = 0; }
        }
        m.sub[0] = 0; ui_cat(m.sub, "SAVE : enregistrer ici"); m.sub_c = C_BRUME;
    }
    if ((int32_t)(ui_synced_until - ui_ms) > 0) {
        char *p;
        m.sub[0] = 0;
        p = ui_cat(m.sub, "Presets reçus : ");
        ui_num(p, ui_pre.n);
        m.sub_c = C_VDG;
    }
    if ((int32_t)(ui_saved_until - ui_ms) > 0) {
        char *p;
        m.sub[0] = 0;
        p = ui_cat(m.sub, "Enregistré dans P");
        ui_num(p, (uint32_t)ui_cur + 1u);
        m.sub_c = C_VDG;
    }

    /* barre du milieu */
    for (i = 0; i < sizeof t; i++) ((char *)&t)[i] = 0;
    if (ui_cur < (int32_t)ui_pre.n) { t.p[0] = 'P'; ui_num(t.p + 1, (uint32_t)ui_cur + 1u); }
    else ui_cat(t.p, "P+");
    ui_copy_name(t.son, SN_TYPES[ui_s.type < SN_NTYPES ? ui_s.type : 1u].name);
    ui_tone(t.ton, ui_s.tone);

    /* les quatre cases des molettes */
    for (i = 0; i < sizeof s; i++) ((char *)&s)[i] = 0;
    ui_cat(s.lab[0], "DÉPART"); ui_cat(s.lab[1], "FADE"); ui_cat(s.lab[2], "RÉVERB");
    for (i = 0; i < 4u; i++) s.vc[i] = C_OS;
    if (ph == SN_WAIT) { ui_mmss(s.val[0], (uint32_t)ui_st.wait_left_s); s.vc[0] = C_LAITON; }
    else if (ph == SN_READY) ui_mmss(s.val[0], ui_s.delay_s);
    else { ui_cat(s.val[0], "—"); s.vc[0] = C_BRUME; }
    ui_mmss(s.val[1], ui_s.fade_s);
    ui_pct(s.val[2], ui_s.reverb);
    if (ph == SN_SONAR || ph == SN_CONT) {
        ui_cat(s.lab[3], "± TEMPS");
        if (r >= 0) ui_mmss(s.val[3], (uint32_t)r);
        else { s.val[3][0] = '+'; ui_mmss(s.val[3] + 1, (uint32_t)(-r)); s.vc[3] = C_SANG; }
    } else {
        ui_cat(s.lab[3], "DURÉE");
        ui_mmss(s.val[3], ui_s.duration_s);
    }
    if (focus) s.active = ui_focus == F_DELAY ? 1u : ui_focus == F_FADE ? 2u : ui_focus == F_REVERB ? 3u : ui_focus == F_DURATION ? 4u : 0u;

    if (!ui_drawn || memcmp(&h, &ui_dh, sizeof h)) { ui_dh = h; ui_draw_head(&h); }
    if (!ui_drawn || memcmp(&m, &ui_dm, sizeof m)) { ui_dm = m; ui_draw_mid(&m); }
    if (!ui_drawn || memcmp(&t, &ui_dt, sizeof t)) { ui_dt = t; ui_draw_meta(&t); }
    if (!ui_drawn || memcmp(&s, &ui_ds, sizeof s)) { ui_ds = s; ui_draw_strip(&s); }
    ui_drawn = 1;
}
