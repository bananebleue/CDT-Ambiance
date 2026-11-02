/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* Banc d'essai de l'interface du sonar sur ordinateur : un FM-1 simulé (molettes, boutons, touches, MIDI),
 * l'écran en images PPM, le son en WAV. Aucun code de la plateforme : seulement sonar_core.c et sonar_ui.c.
 *
 *   cc -O2 -Wall -Ifirmware/sonar -o build/sonar_ui_host tests/sonar_ui_host.c
 *   build/sonar_ui_host build/ui tests/sonar_ui_script.txt      (-> build/ui-NOM.ppm, build/ui.wav)
 *
 * Une ligne du scénario : "T enc ID N" (molette ID, N crans), "T btn ID" (un appui bref), "T down ID" / "T up ID",
 * "T key" (une touche du clavier), "T cc CC V" (contrôleur MIDI, canal 16), "T shot NOM", "T end". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint16_t fb[240 * 240];
static uint8_t store[2][8192];
static uint32_t store_n[2];
static uint32_t leds;

static void ui_hw_sync(void) {}
static void ui_hw_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) fb[(y + j) * 240u + x + i] = px[j * w + i];
}
static void ui_hw_led(uint32_t btn, int on) { if (on) leds |= 1u << btn; else leds &= ~(1u << btn); }
static int ui_hw_load(uint32_t which, void *p, uint32_t n)
{
    if (which > 1u || store_n[which] != n) return -1;
    memcpy(p, store[which], n);
    return 0;
}
static uint32_t saves;
static int ui_hw_save(uint32_t which, const void *p, uint32_t n)
{
    if (which > 1u || n > sizeof store[0]) return -1;
    memcpy(store[which], p, n);
    store_n[which] = n;
    saves++;
    return 0;
}

#include "sonar_core.c"
#include "sonar_ui.c"

static const char *out = "build/ui";
static int16_t *wav;
static uint32_t wav_n;

static void shot(const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s-%s.ppm", out, name);
    if (!(f = fopen(path, "wb"))) { perror(path); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f); fputc((c >> 5 & 63) * 255 / 63, f); fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    printf("écran -> %s\n", path);
}

typedef struct { uint32_t t; char what[8]; int a, b; char name[48]; } ev_t;
static ev_t ev[4096];
static uint32_t nev, end_ms = 10000;

int main(int argc, char **argv)
{
    ui_in_t in;
    uint32_t ms, k = 0, owed = 0, tap_release = 0;
    static int32_t blk[512];
    char line[256];
    FILE *f;
    if (argc > 1) out = argv[1];
    if (argc > 2 && (f = fopen(argv[2], "r"))) {
        while (fgets(line, sizeof line, f) && nev < 4096u) {
            ev_t e;
            memset(&e, 0, sizeof e);
            if (line[0] == '#' || sscanf(line, "%u %7s", &e.t, e.what) != 2) continue;
            if (!strcmp(e.what, "end")) { end_ms = e.t; continue; }
            if (!strcmp(e.what, "shot")) sscanf(line, "%*u %*s %47s", e.name);
            else sscanf(line, "%*u %*s %d %d", &e.a, &e.b);
            ev[nev++] = e;
        }
        fclose(f);
    }
    sn_init();
    ui_init(0);
    for (ms = 0; ms <= end_ms; ms++) {
        memset(&in, 0, sizeof in);
        in.ms = ms;
        in.held = tap_release ? 0 : 0;
        if (tap_release) { in.released = tap_release; tap_release = 0; }
        while (k < nev && ev[k].t <= ms) {
            ev_t *e = &ev[k++];
            static uint32_t held;
            if (!strcmp(e->what, "enc")) in.enc[e->a] += e->b;
            else if (!strcmp(e->what, "btn")) { in.pressed |= 1u << e->a; tap_release |= 1u << e->a; }
            else if (!strcmp(e->what, "down")) { in.pressed |= 1u << e->a; held |= 1u << e->a; }
            else if (!strcmp(e->what, "up")) { in.released |= 1u << e->a; held &= ~(1u << e->a); }
            else if (!strcmp(e->what, "key")) in.keys_pressed |= 1u;
            else if (!strcmp(e->what, "cc")) ui_midi(0xBF, (uint8_t)e->a, (uint8_t)e->b);
            else if (!strcmp(e->what, "shot")) { ui_draw(); shot(e->name); }
            in.held = held;
        }
        {
            static uint32_t held_keep;
            if (in.held) held_keep = in.held;
            if (in.released) held_keep &= ~in.released;
            in.held = held_keep;
        }
        ui_step(&in);
        ui_draw();
        owed += SN_FS;
        while (owed >= 1000u * 256u) {
            uint32_t i;
            owed -= 1000u * 256u;
            memset(blk, 0, sizeof blk);
            sn_render(blk, 256);
            wav = realloc(wav, (wav_n + 256u) * 4u);
            for (i = 0; i < 512u; i++) wav[2u * wav_n + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
            wav_n += 256u;
        }
    }
    {
        char path[512];
        uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, SN_FS, SN_FS * 4u, 0x00100004u};
        snprintf(path, sizeof path, "%s.wav", out);
        f = fopen(path, "wb");
        fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
        fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
        fwrite(wav, 4, wav_n, f);
        fclose(f);
        printf("son -> %s (%.1f s), %u écritures en flash, LED : %#x\n", path, wav_n / 44100.0, saves, leds);
    }
    return 0;
}
