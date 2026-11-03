/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* L'envoi des presets de CDT Compagnon au FM-1 (ui_midi) : la séquence produite par la page (fm1sync.js, rejouée
 * depuis tests/presets_sync_fixture.txt), puis des envois abîmés qui ne doivent rien changer.
 *   cc -O2 -Wall -Ifirmware/sonar -o build/sonar_sync_test tests/sonar_sync_test.c && build/sonar_sync_test tests/presets_sync_fixture.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t store[2][8192];
static uint32_t store_n[2];
static void ui_hw_sync(void) {}
static void ui_hw_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
static void ui_hw_led(uint32_t btn, int level) { (void)btn; (void)level; }
static int ui_hw_load(uint32_t which, void *p, uint32_t n) { if (which > 1u || store_n[which] != n) return -1; memcpy(p, store[which], n); return 0; }
static int ui_hw_save(uint32_t which, const void *p, uint32_t n) { memcpy(store[which], p, n); store_n[which] = n; return 0; }

#include "sonar_core.c"
#include "sonar_ui.c"

static int fails;
static void check(int ok, const char *what) { printf("%s  %s\n", ok ? "ok  " : "ÉCHEC", what); if (!ok) fails++; }

static uint32_t n_msg;
static uint8_t msg[4096][2];

static void replay(uint32_t skip, int32_t bump_last)
{
    uint32_t i;
    for (i = 0; i < n_msg; i++) {
        uint8_t v = msg[i][1];
        if (i == skip) continue;
        if (bump_last && i == n_msg - 1u) v = (uint8_t)((v + 1u) & 127u);
        ui_midi(0xBF, msg[i][0], v);
    }
}

int main(int argc, char **argv)
{
    char line[128];
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/presets_sync_fixture.txt", "r");
    ui_in_t in;
    if (!f) { perror("fixture"); return 1; }
    while (fgets(line, sizeof line, f)) {
        unsigned c, v;
        if (line[0] != '#' && sscanf(line, "%u %u", &c, &v) == 2 && n_msg < 4096u) { msg[n_msg][0] = (uint8_t)c; msg[n_msg][1] = (uint8_t)v; n_msg++; }
    }
    fclose(f);

    sn_init();
    ui_init(0);
    memset(&in, 0, sizeof in);
    check(ui_pre.n == 1u && !strcmp(ui_pre.p[0].name, "Spring's Island"), "au départ : un preset, Spring's Island");

    replay(~0u, 1);                                        /* somme fausse : rien ne change */
    check(ui_pre.n == 1u, "somme de contrôle fausse : presets inchangés");
    replay(5, 0);                                          /* un message perdu (un caractère) : la somme ne colle plus */
    check(ui_pre.n == 1u, "un message perdu : presets inchangés");

    replay(~0u, 0);
    check(ui_pre.n == 3u, "envoi complet : 3 presets");
    check(!strcmp(ui_pre.p[0].name, "Spring's Island") && ui_pre.p[0].s.duration_s == 3600u && ui_pre.p[0].s.fade_s == 20u &&
          ui_pre.p[0].s.reverb == 30u && ui_pre.p[0].s.type == 1u && ui_pre.p[0].s.tone == 0, "P1 : nom et réglages");
    check(!strcmp(ui_pre.p[1].name, "Bath's Astoria tres"), "P2 : nom en ASCII, coupé à 19");
    check(ui_pre.p[1].s.delay_s == 125u && ui_pre.p[1].s.duration_s == 43200u && ui_pre.p[1].s.reverb == 100u &&
          ui_pre.p[1].s.type == 5u && ui_pre.p[1].s.tone == -24, "P2 : 12 h, réverb 100, type 6, -24 demi-tons");
    check(ui_pre.p[2].s.delay_s == 3u && ui_pre.p[2].s.fade_s == 0u && ui_pre.p[2].s.duration_s == 90u && ui_pre.p[2].s.tone == 12,
          "P3 : réglages");
    check(ui_s.duration_s == 3600u, "au repos : le preset affiché (P1) est rechargé");
    in.ms = 10000u;
    ui_step(&in);
    check(store_n[1] == sizeof ui_pre && ((ui_presets_t *)store[1])->n == 3u, "au repos, en silence : enregistré en flash");
    printf("P2 : « %s »\n", ui_pre.p[1].name);
    printf(fails ? "%d échec(s)\n" : "toutes les vérifications passent\n", fails);
    return fails != 0;
}
