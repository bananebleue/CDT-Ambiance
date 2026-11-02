#!/usr/bin/env python3
"""Retrouve dans build/spring_sonar.dis la fonction et les instructions autour des adresses d'un écran de plantage.
  crash_lookup.py ADRESSE [ADRESSE ...]     (hexadécimal, comme sur l'écran : 02001FF0)"""
import re
import sys
from pathlib import Path

dis = Path(__file__).resolve().parents[1].joinpath("build", "spring_sonar.dis").read_text().splitlines()
FUNC = re.compile(r"^([0-9a-f]+) <(.+)>:")
INS = re.compile(r"^\s*([0-9a-f]+):")
for a in sys.argv[1:]:
    addr = int(a, 16)
    func, best = None, None
    for i, ln in enumerate(dis):
        m = FUNC.match(ln)
        if m and int(m.group(1), 16) <= addr:
            func = (m.group(2), int(m.group(1), 16))
        m = INS.match(ln)
        if m and int(m.group(1), 16) == addr:
            best = i
            break
    print(f"===== {addr:#010x} : {func[0] if func else '?'} + {addr - func[1]:#x}" if func else f"===== {addr:#010x}")
    if best is not None:
        start = best
        while start > 0 and not FUNC.match(dis[start]) and best - start < 60:
            start -= 1
        for ln in dis[start:best + 12]:
            print((">> " if ln is dis[best] else "   ") + ln)
