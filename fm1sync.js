/* SPDX-License-Identifier: GPL-3.0-only
 * CDT Compagnon → Spring's Sonar (FM-1) : l'heure et les presets, en contrôleurs MIDI sur le canal 16.
 * Le protocole est décrit dans fm1/firmware/sonar/sonar_ui.c (ui_midi) ; la page est la référence des presets.
 */
(function (root) {
'use strict';
const MAX_PRESETS = 32, NAME_MAX = 19;

// un nom tel que le FM-1 sait l'afficher : ASCII, sans accents, 19 caractères au plus
function asciiName(s) {
  const t = String(s || '').normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/[’‘]/g, "'").replace(/[^\x20-\x7E]/g, '').replace(/\s+/g, ' ').trim().slice(0, NAME_MAX).trim();
  return t || 'Preset';
}

/** [[cc, valeur], ...] pour l'heure de `date` (h, min, s, valider) */
function timeMessages(date) {
  return [[102, date.getHours()], [103, date.getMinutes()], [104, date.getSeconds()], [105, 0]];
}

/** [[cc, valeur], ...] qui remplacent tous les presets du FM-1 par `presets` (au plus 32) */
function presetMessages(presets) {
  const list = presets.slice(0, MAX_PRESETS);
  const out = [[113, list.length]];
  let sum = 0;
  const put = (cc, v) => { out.push([cc, v & 0x7F]); if ((cc >= 110 && cc <= 111) || (cc >= 120 && cc <= 123)) sum += v & 0x7F; };
  const val = (id, v) => {
    v = Math.max(0, Math.round(v));
    put(120, v & 0x7F); put(121, (v >> 7) & 0x7F); put(122, (v >> 14) & 0x7F); put(123, id);
  };
  list.forEach((p, i) => {
    put(110, i);
    for (const ch of asciiName(p.name)) put(111, ch.charCodeAt(0));
    val(1, p.delay); val(2, p.fade); val(3, p.duration); val(4, p.reverb); val(5, p.type); val(6, (p.tone | 0) + 24);
    out.push([112, 0]);
  });
  out.push([114, sum & 0x7F]);
  return out;
}

const API = { timeMessages, presetMessages, asciiName, MAX_PRESETS };
if (typeof module !== 'undefined' && module.exports) module.exports = API; else root.FM1Sync = API;
})(typeof self !== 'undefined' ? self : this);
