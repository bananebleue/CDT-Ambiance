/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Stéphane Faure (CDT Ambiance) */
/* Banc d'essai du cœur du sonar sur ordinateur (sans la plateforme FM-1) :
 * une partie accélérée (2 min de sonar), des vérifications, et le son en WAV.
 *
 *   cc -O2 -Wall -Ifirmware/sonar -o build/sonar_core_test tests/sonar_core_test.c && build/sonar_core_test build/sonar_core.wav
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sonar_core.c"

#define BLK 256u
static int16_t *wav;
static uint32_t wav_n, fails;
static double cpu_s;

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "ÉCHEC", what);
    if (!ok) fails++;
}

static void run_seconds(double sec, sn_status_t *st)    /* fait tourner l'interruption audio */
{
    uint32_t blocks = (uint32_t)(sec * SN_FS / BLK), b, i;
    static int32_t out[2 * BLK];
    for (b = 0; b < blocks; b++) {
        clock_t c0 = clock();
        memset(out, 0, sizeof out);
        sn_render(out, BLK);
        cpu_s += (double)(clock() - c0) / CLOCKS_PER_SEC;
        wav = realloc(wav, (wav_n + BLK) * 4u);
        for (i = 0; i < 2u * BLK; i++) {
            int32_t v = out[i];
            if (v > 32767 || v < -32767) { static int warned; if (!warned++) printf("note : écrêtage à %.2f s\n", wav_n / 44100.0); }
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += BLK;
    }
    sn_status(st);
}

static void write_wav(const char *name)
{
    FILE *f = fopen(name, "wb");
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, SN_FS, SN_FS * 4u, 0x00100004u};
    if (!f) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("son -> %s (%.1f s)\n", name, wav_n / 44100.0);
}

int main(int argc, char **argv)
{
    sn_settings_t s = {3, 5, 120, 70, 30, 1, 0};   /* départ 3 s, fade 5 s, 2 min, volume 70, réverb 30, Coque */
    sn_status_t st;
    uint32_t p0, p1;
    sn_init();
    sn_set(&s);

    run_seconds(0.5, &st);
    check(st.phase == SN_READY, "au repos : Prêt");
    sn_cmd(SN_CMD_PREVIEW, 0);                     /* écouter avant le départ */
    run_seconds(3.0, &st);

    sn_cmd(SN_CMD_LAUNCH, 0);
    run_seconds(1.0, &st);
    check(st.phase == SN_WAIT && (st.wait_left_s == 2 || st.wait_left_s == 3), "lancé : Attente, ~2 s restantes");
    run_seconds(2.5, &st);
    check(st.phase == SN_SONAR, "après le délai : Sonar");
    check(st.remaining_s == 120 || st.remaining_s == 119, "durée restante ~120 s");
    check(st.interval_ms > 7900 && st.interval_ms <= 8000, "premier intervalle ~8 s");

    run_seconds(40.0, &st);                        /* r ~ 80 s */
    printf("à %d s restantes : ping toutes les %u ms, pong après %u ms\n", st.remaining_s, st.interval_ms, st.echo_ms);
    {
        uint32_t want = st.interval_ms * 45u / 100u;
        if (want > 1800u) want = 1800u;
        check(st.echo_ms + 2u >= want && st.echo_ms <= want + 2u, "écart ping-pong = min(1,8 s ; 45 % de l'intervalle)");
    }

    p0 = st.pings;
    sn_cmd(SN_CMD_MUTE, 0);
    run_seconds(10.0, &st);
    check(st.muted == 1 && st.phase == SN_SONAR, "son coupé, le décompte continue");
    check(st.pings > p0, "les pings continuent en silence");
    sn_cmd(SN_CMD_PREVIEW, 0);                     /* écouter quand le son est coupé */
    run_seconds(3.0, &st);
    sn_cmd(SN_CMD_UNMUTE_FADE, 0);
    run_seconds(6.0, &st);
    check(st.muted == 0, "son rétabli en fondu");

    sn_cmd(SN_CMD_REMAINING, 30);                  /* le MJ resserre : plus que 30 s */
    run_seconds(0.2, &st);
    check(st.remaining_s == 30 || st.remaining_s == 29, "temps restant ramené à 30 s");
    run_seconds(31.0, &st);
    check(st.phase == SN_CONT, "à zéro : bip continu");
    run_seconds(4.0, &st);
    check(st.remaining_s <= -4 && st.remaining_s >= -5, "dépassement compté (+4 s)");

    sn_cmd(SN_CMD_REMAINING, 20);                  /* on rajoute 20 s pendant le bip continu */
    run_seconds(1.0, &st);
    check(st.phase == SN_SONAR, "temps rajouté : retour au sonar");
    p1 = st.pings;
    run_seconds(21.0, &st);
    check(st.phase == SN_CONT && st.pings > p1, "nouveaux pings puis nouveau bip continu");
    check(st.interval_ms < 1000, "fin de course : intervalle sous 1 s");

    sn_cmd(SN_CMD_STOP, 0);
    run_seconds(2.0, &st);
    check(st.phase == SN_READY, "arrêt : retour à Prêt");

    write_wav(argc > 1 ? argv[1] : "build/sonar_core.wav");
    printf("calcul : %.3f µs par échantillon sur cet ordinateur\n", cpu_s * 1e6 / wav_n);
    printf("%s\n", fails ? "DES VÉRIFICATIONS ONT ÉCHOUÉ" : "toutes les vérifications passent");
    return fails ? 1 : 0;
}
