#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Stéphane Faure (CDT Ambiance)
"""Génère firmware/sonar/sonar_tables.h : toutes les constantes du sonar en nombres entiers.

La puce du FM-1 n'a pas de calcul à virgule : on calcule ici, en Python, ce qui demande des flottants
(sinus, exponentielles, puissances, gains de réverbération) et on l'écrit sous forme d'entiers.

    python3 tools/gen_sonar_tables.py            (depuis fm1/)
"""
import math
from pathlib import Path

FS = 44100
OUT = Path(__file__).resolve().parent.parent / "firmware" / "sonar" / "sonar_tables.h"

# --- les six types de son (mêmes valeurs que la page web CDT Compagnon)
TYPES = [
    # nom, f0, partiels (rapport, dB), attaque s, t60 du son direct, RT60 réverb, niveau réverb dB, clic dB, pong dB
    ("Abysse",    1040, [(1.0, 0)],                                   0.040, 0.6, 3.3, -11, None, 0),
    ("Coque",     1190, [(1.0, 0), (1.125, -34)],                     0.010, 0.9, 3.0, -9,  None, 0),
    ("Métal",     1034, [(1.0, 0), (1.16, -18), (2.0, -24), (3.0, -30)], 0.040, 1.1, 1.2, -18, None, -4),
    ("Clic",      1100, [(1.0, 0)],                                   0.050, 0.6, 2.5, -9,  -8,   0),
    ("Sonde",     1040, [(1.0, 0)],                                   0.030, 0.7, 1.0, -17, None, -3),
    ("Impulsion", 1100, [(1.0, 0)],                                   0.040, 0.5, 0.8, -16, -8,   -3),
]
GENERAL_RT60 = 2.2           # réverbération générale (molette ALGORITHM)
PONG_T60, PONG_ATTACK = 0.45, 0.005
CONT_ATTACK, CONT_RELEASE_T60 = 0.020, 0.15
CLICK_TAU = 0.015

# --- réverbération de type Freeverb, mono (longueurs d'origine à 44,1 kHz)
COMBS = [1116, 1188, 1277, 1356]
ALLPASS = [556, 441]
DAMP = 0.25                  # amortissement des aigus dans les boucles

# --- courbes du sonar
I_MAX, I_MIN, K_CURVE = 7.0, 0.25, 0.7
E_MAX, E_RATIO = 1.8, 0.45

def db(v):
    return 10 ** (v / 20)

def q(v, bits):
    return int(round(v * (1 << bits)))

def freeverb_energy(rt60):
    """Énergie de la réponse impulsionnelle de notre réverbération (même algorithme qu'en C, en flottant)."""
    gains = [10 ** (-3 * L / (rt60 * FS)) for L in COMBS]
    n = int(FS * rt60 * 1.5)
    combs = [[0.0] * L for L in COMBS]
    idx = [0] * len(COMBS)
    store = [0.0] * len(COMBS)
    aps = [[0.0] * L for L in ALLPASS]
    aidx = [0] * len(ALLPASS)
    e = 0.0
    for t in range(n):
        x = 1.0 if t == 0 else 0.0
        acc = 0.0
        for c in range(len(COMBS)):
            out = combs[c][idx[c]]
            store[c] = out * (1 - DAMP) + store[c] * DAMP
            combs[c][idx[c]] = x + store[c] * gains[c]
            idx[c] = (idx[c] + 1) % COMBS[c]
            acc += out
        for a in range(len(ALLPASS)):
            bo = aps[a][aidx[a]]
            y = -acc + bo
            aps[a][aidx[a]] = acc + bo * 0.5
            aidx[a] = (aidx[a] + 1) % ALLPASS[a]
            acc = y
        e += acc * acc
    return gains, e

def main():
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* Généré par tools/gen_sonar_tables.py : ne pas modifier à la main. */",
             "#ifndef SONAR_TABLES_H", "#define SONAR_TABLES_H", ""]

    # sinus : 2048 points + 1 (interpolation linéaire), Q15
    sine = [int(round(32767 * math.sin(2 * math.pi * i / 2048))) for i in range(2049)]
    lines.append("static const int16_t SN_SINE[2049] = {")
    for i in range(0, 2049, 16):
        lines.append("    " + ", ".join(str(v) for v in sine[i:i + 16]) + ",")
    lines.append("};")

    # x^0.7 sur [0, 1] : 257 points, Q16
    pw = [int(round(65536 * (i / 256) ** K_CURVE)) for i in range(257)]
    lines.append("static const uint32_t SN_POW07[257] = {")
    for i in range(0, 257, 16):
        lines.append("    " + ", ".join(str(v) for v in pw[i:i + 16]) + ",")
    lines.append("};")

    # 2^(n/12) pour n = -24..24, Q16
    semi = [int(round(65536 * 2 ** (n / 12))) for n in range(-24, 25)]
    lines.append("static const uint32_t SN_SEMI[49] = {   /* index n + 24 */")
    for i in range(0, 49, 13):
        lines.append("    " + ", ".join(str(v) for v in semi[i:i + 13]) + ",")
    lines.append("};")

    # courbes, en échantillons
    lines += ["",
              f"#define SN_I_MAX {int(round(I_MAX * FS))}u   /* {I_MAX} s */",
              f"#define SN_I_MIN {int(round(I_MIN * FS))}u   /* {I_MIN} s */",
              f"#define SN_E_MAX {int(round(E_MAX * FS))}u   /* {E_MAX} s */",
              f"#define SN_E_RATIO_Q16 {q(E_RATIO, 16)}u   /* {E_RATIO} */",
              f"#define SN_PONG_DECAY_Q30 {q(math.exp(-6.91 / (PONG_T60 * FS)), 30)}u",
              f"#define SN_PONG_ATTACK_STEP {int((1 << 30) / (PONG_ATTACK * FS))}u",
              f"#define SN_CONT_ATTACK_STEP {int((1 << 30) / (CONT_ATTACK * FS))}u",
              f"#define SN_CONT_RELEASE_Q30 {q(math.exp(-6.91 / (CONT_RELEASE_T60 * FS)), 30)}u",
              f"#define SN_CLICK_DECAY_Q15 {q(math.exp(-1 / (CLICK_TAU * FS)), 15)}",
              f"#define SN_CLICK_HP_Q15 {q(1 - math.exp(-2 * math.pi * 500 / FS), 15)}",
              f"#define SN_CLICK_LP_Q15 {q(1 - math.exp(-2 * math.pi * 4000 / FS), 15)}",
              f"#define SN_DAMP1_Q15 {q(DAMP, 15)}",
              f"#define SN_DAMP2_Q15 {q(1 - DAMP, 15)}",
              f"#define SN_COMB_LEN {{{', '.join(map(str, COMBS))}}}",
              f"#define SN_AP_LEN {{{', '.join(map(str, ALLPASS))}}}",
              f"#define SN_COMB_TOTAL {sum(COMBS)}",
              f"#define SN_AP_TOTAL {sum(ALLPASS)}",
              f"#define SN_DUCK_Q15 {q(db(-7), 15)}   /* -7 dB */",
              ""]

    # types
    lines.append("typedef struct {")
    lines.append("    const char *name;")
    lines.append("    uint8_t np;                 /* partiels */")
    lines.append("    uint32_t inc[4];            /* pas de phase par échantillon, ton 0 : f * 2^32 / 44100 */")
    lines.append("    int16_t gain[4];            /* niveau de chaque partiel, Q15 */")
    lines.append("    uint32_t attack_step;       /* montée de l'enveloppe Q30 par échantillon */")
    lines.append("    uint32_t decay_q30;         /* décroissance du ping, facteur par échantillon Q30 */")
    lines.append("    int16_t comb_fb[4];         /* rétroaction des 4 filtres en peigne de sa réverb, Q15 */")
    lines.append("    int32_t wet_q15;            /* niveau de sortie de sa réverb, Q15 (énergie normalisée) */")
    lines.append("    int16_t click_q15;          /* niveau du clic, 0 = sans */")
    lines.append("    int16_t pong_q15;           /* niveau du pong */")
    lines.append("} sn_type_t;")
    lines.append("static const sn_type_t SN_TYPES[6] = {")
    for name, f0, partials, attack, t60, rt60, wet, click, pong in TYPES:
        gains, energy = freeverb_energy(rt60)
        wet_lin = db(wet) * 4 / math.sqrt(energy)
        incs = [int(round(f0 * r * 2 ** 32 / FS)) for r, _ in partials] + [0] * (4 - len(partials))
        pg = [q(db(g), 15) if q(db(g), 15) < 32768 else 32767 for _, g in partials] + [0] * (4 - len(partials))
        lines.append(f"    {{\"{name}\", {len(partials)}, {{{', '.join(f'{v}u' for v in incs)}}}, {{{', '.join(map(str, pg))}}},")
        lines.append(f"     {int((1 << 30) / (attack * FS))}u, {q(math.exp(-6.91 / (t60 * FS)), 30)}u,")
        lines.append(f"     {{{', '.join(str(q(g, 15)) for g in gains)}}}, {q(wet_lin, 15)}, "
                     f"{0 if click is None else q(db(click), 15)}, {min(32767, q(db(pong), 15))}}},")
    lines.append("};")

    gains, energy = freeverb_energy(GENERAL_RT60)
    lines.append(f"static const int16_t SN_GEN_COMB_FB[4] = {{{', '.join(str(q(g, 15)) for g in gains)}}};")
    lines.append(f"#define SN_GEN_NORM_Q15 {q(1.2 / math.sqrt(energy), 15)}   /* réverb générale à 100 % : 1,2 x (énergie normalisée) */")
    lines += ["", "#endif", ""]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print("->", OUT)

if __name__ == "__main__":
    main()
