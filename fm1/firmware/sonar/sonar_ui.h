/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* L'interface du sonar sur le FM-1 : molettes, boutons, touches, presets, écran.
 * Sans dépendance au matériel : l'application fournit les crochets ui_hw_* (écran, LED, mémoire flash),
 * le banc d'essai tests/sonar_ui_host.c aussi. */
#ifndef SONAR_UI_H
#define SONAR_UI_H
#include <stdint.h>
#include "sonar_core.h"

/* les numéros du FM-1 (comme la plateforme Dinghy : docs/API.md) */
enum { UI_ENC_SELECT = 0, UI_ENC_ALGORITHM, UI_ENC_K1, UI_ENC_K2, UI_ENC_K3, UI_ENC_K4, UI_ENC_PRESETS, UI_NENC };
enum { UI_BTN_OCT_DOWN = 0, UI_BTN_OCT_UP = 1, UI_BTN_SAVE = 9, UI_BTN_PLAY = 12, UI_BTN_REC = 13 };

typedef struct {
    uint32_t ms;                       /* millisecondes depuis la mise sous tension */
    int32_t enc[UI_NENC];              /* crans tournés depuis le dernier appel (+ = sens horaire) */
    uint32_t pressed, released, held;  /* boutons : appuyés, relâchés depuis le dernier appel ; tenus */
    uint32_t keys_pressed;             /* touches du clavier appuyées depuis le dernier appel */
} ui_in_t;

/* à fournir par l'application */
static void ui_hw_sync(void);                                          /* attendre la fin du dernier envoi à l'écran */
static void ui_hw_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t *px);   /* px en RGB565 ; peut le modifier */
static void ui_hw_led(uint32_t btn, int on);
static int ui_hw_load(uint32_t which, void *p, uint32_t n);            /* which 0 : réglages, 1 : presets ; 0 = lu */
static int ui_hw_save(uint32_t which, const void *p, uint32_t n);

static void ui_init(uint32_t ms);
static void ui_step(const ui_in_t *in);                                /* lire les commandes, piloter le sonar */
static void ui_midi(uint8_t st, uint8_t d1, uint8_t d2);               /* heure et noms de presets depuis le compagnon */
static void ui_draw(void);                                             /* redessiner ce qui a changé */
static int ui_can_save_now(void);                                      /* rien ne sonne : une écriture en flash est permise */

#endif
