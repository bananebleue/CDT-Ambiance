/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* Le cœur du sonar Spring's Island : déroulé (attente, sonar, bip continu), ping, pong, réverbérations.
 * Entiers seulement (la puce n'a pas de virgule flottante), aucune dépendance au matériel :
 * le même code tourne sur le FM-1 et sur ordinateur (tests/sonar_host.c).
 *
 * Deux côtés :
 *   - la boucle principale règle (sn_set), envoie des commandes (sn_cmd) et lit l'état (sn_status) ;
 *   - l'interruption audio appelle sn_render() : elle seule fait avancer le temps et produit le son.
 * Les commandes passent par une petite file (un écrivain, un lecteur), l'état par des variables volatiles. */
#ifndef SONAR_CORE_H
#define SONAR_CORE_H
#include <stdint.h>

#define SN_FS 44100u
#define SN_NTYPES 6u
#define SN_MAX_SECONDS 43200u          /* 12 h : au-delà, le compteur d'échantillons 32 bits ne suffirait plus */

enum { SN_READY = 0, SN_WAIT, SN_SONAR, SN_CONT };

enum {
    SN_CMD_LAUNCH = 1,                 /* Prêt -> Attente (ou Sonar si le délai est nul) */
    SN_CMD_STOP,                       /* tout arrêter, revenir à Prêt */
    SN_CMD_MUTE,                       /* couper le son ; le décompte continue */
    SN_CMD_UNMUTE,                     /* rétablir tout de suite */
    SN_CMD_UNMUTE_FADE,                /* rétablir en fondu (durée du fade-in) */
    SN_CMD_WAIT_LEFT,                  /* arg : secondes d'attente restantes */
    SN_CMD_REMAINING,                  /* arg : secondes de sonar restantes (0 = bip continu tout de suite) */
    SN_CMD_PREVIEW                     /* un ping et son pong au rythme actuel (ou un bout de bip continu) */
};

typedef struct {
    uint32_t delay_s, fade_s, duration_s;
    uint8_t volume, reverb, type;      /* volume et réverb 0..100 ; type 0..5 */
    int8_t tone;                       /* -24..24 demi-tons */
} sn_settings_t;

typedef struct {
    uint8_t phase, muted;
    int32_t wait_left_s;               /* Attente : secondes avant le départ (arrondi au-dessus) */
    int32_t remaining_s;               /* Sonar : secondes restantes (arrondi au-dessus) ; Bip continu : - dépassement */
    uint32_t interval_ms, echo_ms;     /* rythme actuel */
    uint32_t pings;                    /* compteur de pings (pour faire clignoter une LED) */
} sn_status_t;

static void sn_init(void);
static void sn_set(const sn_settings_t *s);
static int sn_cmd(uint32_t cmd, int32_t arg);
static void sn_status(sn_status_t *st);
static void sn_render(int32_t *out, uint32_t n);

#endif
