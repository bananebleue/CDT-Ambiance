/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* Spring's Sonar : le firmware du sonar de Spring's Island (Les Chants de Tindalos) pour le M-VAVE FM-1.
 *
 * Ce fichier ne fait que raccorder le sonar à la plateforme Dinghy (platform.c : mises à jour, secours USB,
 * USB-MIDI, écran, molettes, boutons, mémoire flash). Le sonar lui-même :
 *   firmware/sonar/sonar_core.c   le déroulé et le son (entiers seulement)
 *   firmware/sonar/sonar_ui.c     l'interface et l'écran
 * Les deux tournent aussi sur ordinateur : tests/sonar_core_test.c, tests/sonar_ui_host.c.
 *
 *   molettes 1..4   délai avant départ, fade-in, durée (± temps en jeu), volume
 *   ALGORITHM       réverbération générale        SELECT    type de son (une touche le fait entendre)
 *   OCT- / OCT+     tonalité, un demi-ton          PRESETS   faire défiler, « + Nouveau » à la fin
 *   PLAY            lancer ; ensuite couper / rétablir (appui long : en fondu)
 *   REC maintenu    arrêter et réinitialiser       SAVE      enregistrer dans le preset affiché
 *   MASTER          le volume de sortie (platform.c) */
#ifndef APP_NAME
#define APP_NAME "SPRING SONAR"
#endif
#define CV_MAX (240u * 16u)                     /* le canevas de la plateforme sert peu : le sonar a son propre tampon */
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"

#define SN_POOL __attribute__((section(".pool")))
#include "sonar_core.c"
#include "sonar_ui.c"

/* ---- les crochets de l'interface */
static void ui_hw_sync(void) { lcd_sync(); }
static void ui_hw_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t *px)
{
    uint32_t i, n = w * h;
    for (i = 0; i < n; i++) px[i] = swap16(px[i]);    /* l'écran prend le RGB565 octets inversés */
    lcd_blit(x, y, w, h, px);
}
static void ui_hw_led(uint32_t btn, int on) { fm1_led_key(btn, on); }
static int ui_hw_load(uint32_t which, void *p, uint32_t n) { return which ? blob_load(0, p, n) : settings_load(p, n); }
static int ui_hw_save(uint32_t which, const void *p, uint32_t n) { return which ? blob_save(0, p, n) : settings_save(p, n); }

/* ---- le son : l'interruption audio */
static void app_audio(int32_t *out, uint32_t n) { sn_render(out, n); }

/* ---- MIDI : l'heure et les noms de presets envoyés par CDT Compagnon */
static uint32_t idle_ok;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0)
        ui_midi(st, d1, d2);
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

static void app_main(void)
{
    ui_in_t in;
    uint32_t prev_keys = 0, e;
    sn_init();
    ui_init(fm1_ms);
    lcd_fill(0, 0, 240, 240, RGB(0x0A, 0x12, 0x14));
    for (;;) {
        uint32_t keys, rel = 0;
        platform_poll();
        in.ms = fm1_ms;
        for (e = 0; e < UI_NENC; e++) in.enc[e] = fm1_enc_take(e);
        in.pressed = fm1_input_edges(&rel);
        in.released = rel;
        in.held = fm1_in.buttons;
        keys = fm1_in.notes;
        in.keys_pressed = (keys & ~prev_keys) | fm1_input_note_edges();
        prev_keys = keys;
        midi_in();
        ui_step(&in);
        idle_ok = 1;                            /* pendant le dessin, le MIDI continue d'être lu */
        ui_draw();
        idle_ok = 0;
    }
}
