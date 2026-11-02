/* SPDX-License-Identifier: GPL-3.0-only
 * Installateur de firmware FM-1 par Web MIDI (CDT Compagnon).
 * Transposition de tools/fm1_install.py (Dinghy / Felucca, © 2026 Leo Kuroshita, Hügelton Instruments) :
 * même format de paquet, mêmes contrôles, même dialogue « l'appareil demande, l'hôte répond ».
 *   étape 1 : le firmware en place lit une partie du paquet et démarre le chargeur de mise à jour ;
 *   étape 2 : le chargeur lit tout le paquet et l'écrit ; puis le FM-1 redémarre et on vérifie l'identité.
 */
(function (root) {
'use strict';

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000;
const MAXDATA = 512, BLOCKS = 20, BLK = 0x30, KEEP = 0x2F, MAX_BLOCKS = 64;
const LOADER_MARK = [...'FELUCCA-LOADER-1'].map(c => c.charCodeAt(0));
const PORT_RE = /fm-1|fm1|m-vave|mvave|smk|elite|mke-p37|starry|felucca|sloop|clipper|melodee|fomni|x0x|makkuro|maku|rainbow|snoop|vector|vox|ota|composite|sinco|usb-midi/i;
const OWN_LOADER_RE = /^ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d$/i;
const SMK_FAMILY = /^(SMK-37[ +].*|MKE-P37|STARRYKEY-37.*)$/i;

// millisecondes ; les tests les raccourcissent
const DELAY = { open: 600, start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000,
                poll: 1000, hs: 1000, idle_check: 8000, idle_write: 180000,
                wait_loader: 30000, wait_reboot: 40000 };

class InstallError extends Error {
  constructor(code, msg) { super(msg); this.code = code; }
}

const wait = ms => new Promise(r => setTimeout(r, ms));
const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

/* ------------------------------------------------------------ format filaire --- */

function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

const le32 = (u, i) => (u[i] | (u[i + 1] << 8) | (u[i + 2] << 16) | (u[i + 3] << 24)) >>> 0;
const le16 = (u, i) => u[i] | (u[i + 1] << 8);

function response(addr, data, fl = 0) {
  const n = data.length;
  const body = [0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    n & 0xFF, (n >> 8) & 0xFF, 0];
  for (let i = 0; i < n; i++) body.push(data[i]);
  let s = 0; for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0x00 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0; for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: le32(u, 7), n: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.slice(1, -1));
  if (d.length < 7 || d[0] !== 0x00 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  let txt = '';
  for (let i = 6; i < Math.min(d.length, 33) && d[i]; i++) txt += String.fromCharCode(d[i]);
  const m = txt.match(/^([^_]+)_(\d+)$/);
  return m ? { text: txt, model: m[1], version: +m[2], loader: m[1].toLowerCase().startsWith('ota-') } : null;
}

/* ------------------------------------------------------------------ paquet --- */

const idChar = (m, i) => { const c = (m - i - 1) & 0xFF; return c >= 0x20 && c < 0x7F; };

function identityBlocks(raw) {
  const marker = k => ((k + 1) * BLK <= raw.length ? raw[k * BLK + KEEP] : null);
  let n = BLOCKS, m;
  while (n < MAX_BLOCKS && (m = marker(n)) !== null && m !== 0x7D && idChar(m, n)) n++;
  return n > BLOCKS && marker(n) === 0x7D ? n : BLOCKS;
}

function blockCount(raw) {
  let n = identityBlocks(raw);
  while (n < MAX_BLOCKS && (n + 1) * BLK <= raw.length && raw[n * BLK + KEEP] === 0x7D) n++;
  return n;
}

function logicalImage(raw, blocks = BLOCKS) {
  const tail = raw.length - blocks * BLK;
  const out = new Uint8Array(blocks * KEEP + Math.max(0, tail));
  for (let i = 0; i < blocks; i++) out.set(raw.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  if (tail > 0) out.set(raw.subarray(blocks * BLK), blocks * KEEP);
  return out;
}

function deinterleave(raw) {
  const n = blockCount(raw);
  const image = logicalImage(raw, n);
  if (n !== BLOCKS && packageDamage(image)) {
    const alt = logicalImage(raw, BLOCKS);
    if (!packageDamage(alt)) return { image: alt, blocks: BLOCKS };
  }
  return { image, blocks: n };
}

function productOf(raw) {
  if (raw.length < BLOCKS * BLK) throw new InstallError('badpkg', 'ce n’est pas un paquet FM-1 (trop court)');
  const ib = identityBlocks(raw);
  const n = ib === BLOCKS ? BLOCKS : Math.min(ib, deinterleave(raw).blocks);
  let s = '';
  for (let i = 0; i < n; i++) { const m = raw[i * BLK + KEEP]; if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF); }
  return s;
}

function crc16(data, crc = 0) {      // CRC-16/XMODEM, comme le paquet et l'appareil
  for (let k = 0; k < data.length; k++) {
    crc ^= data[k] << 8;
    for (let j = 0; j < 8; j++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
  }
  return crc;
}

function dec(b) {                    // chiffrement de flux JieLi ENC (son propre inverse)
  const out = Uint8Array.from(b); let key = 0xFFFF;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xFF;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
  return out;
}

function packageDamage(image) {
  if (image.length < 0x40) return 'trop court';
  const hdr = dec(image.subarray(0, 0x40));
  if (crc16(hdr.subarray(2, 0x40)) !== le16(hdr, 0)) return 'CRC de l’en-tête';
  const count = le16(hdr, 8);
  if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return 'liste des fichiers';
  const lst = image.subarray(0x40, 0x40 + 0x50 * count);
  if (crc16(lst) !== le16(hdr, 2)) return 'CRC de la liste des fichiers';
  for (let i = 0; i < count; i++) {
    const e = dec(lst.subarray(i * 0x50, (i + 1) * 0x50));
    const dcrc = le16(e, 4), off = le32(e, 8), size = le32(e, 12), typ = le16(e, 0);
    let name = ''; for (let j = 0x40; j < 0x50 && e[j]; j++) name += String.fromCharCode(e[j]);
    if (off + size > image.length) return name + ' : tronqué';
    if ((typ === 0 || typ === 100) && crc16(image.subarray(off, off + size)) !== dcrc) return name + ' : CRC';
  }
  return null;
}

function hasMarker(raw) {
  outer: for (let i = 0; i + LOADER_MARK.length <= raw.length; i++) {
    if (raw[i] !== LOADER_MARK[0]) continue;
    for (let j = 1; j < LOADER_MARK.length; j++) if (raw[i + j] !== LOADER_MARK[j]) continue outer;
    return true;
  }
  return false;
}

/** raw : Uint8Array du fichier .fwsc → { product, image } ou InstallError */
function loadPackage(raw, force = false) {
  const product = productOf(raw);
  if (!/^[^_]+_\d+$/.test(product)) throw new InstallError('badpkg', 'ce n’est pas un paquet FM-1 (identité « ' + product + ' »)');
  if (!hasMarker(raw) && !force)
    throw new InstallError('badpkg', 'ce paquet n’a pas le chargeur de mise à jour Felucca : seuls les paquets Felucca/Dinghy '
      + '(comme Spring’s Sonar) s’installent ici. Pour la V15 officielle, utilise le bouton « official V15 » du Firmware Hub.');
  const { image } = deinterleave(raw);
  const bad = packageDamage(image);
  if (bad) throw new InstallError('badpkg', 'paquet abîmé (' + bad + ') : rien n’a été écrit. Télécharge-le à nouveau.');
  return { product, image };
}

const modelOf = t => t.split('_')[0].replace(/^ota-/, '');
function sameKeyboard(a, b) {
  const ma = modelOf(a), mb = modelOf(b);
  return ma === mb || (SMK_FAMILY.test(ma) && SMK_FAMILY.test(mb));
}

/* -------------------------------------------------------------- Web MIDI --- */

const baseName = n => n.replace(/\s+\d+$/, '');

class Link {
  constructor(access, input, output) {
    this.access = access; this.input = input; this.output = output;
    this.q = []; this.waiters = [];
    this.handler = m => {
      const d = m.data;
      if (d[0] !== 0xF0) return;
      const pkt = Array.from(d);
      const w = this.waiters.shift();
      if (w) w(pkt); else this.q.push(pkt);
    };
    input.addEventListener('midimessage', this.handler);
  }
  async open() {
    await Promise.all([this.input.open(), this.output.open()]);
    await wait(DELAY.open);       // Android et CoreMIDI perdent ce qui part juste après l'ouverture
  }
  get lost() {
    return this.input.state === 'disconnected' || !this.access.inputs.has(this.input.id);
  }
  send(pkt) {
    try { this.output.send(pkt); return true; } catch (e) { return false; }
  }
  read(timeout) {
    if (this.q.length) return Promise.resolve(this.q.shift());
    return new Promise(res => {
      let w;
      const t = setTimeout(() => { const i = this.waiters.indexOf(w); if (i >= 0) this.waiters.splice(i, 1); res(null); }, Math.max(0, timeout));
      w = pkt => { clearTimeout(t); res(pkt); };
      this.waiters.push(w);
    });
  }
  drain() { this.q.length = 0; }
  close() {
    this.input.removeEventListener('midimessage', this.handler);
    try { this.input.close(); } catch (e) { /* déjà fermé */ }
    try { this.output.close(); } catch (e) { /* déjà fermé */ }
  }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let k = 0; k < tries; k++) {
    if (!link.send(HS_QUERY)) return null;
    const end = now() + DELAY.hs;
    for (;;) {
      const left = end - now();
      if (left <= 0) break;
      const p = await link.read(left);
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

class Updater {
  constructor(access) { this.access = access; }

  candidates() {
    return [...this.access.inputs.values()].filter(p => p.state !== 'disconnected' && PORT_RE.test(p.name || ''));
  }

  pairOutput(input) {
    const outs = [...this.access.outputs.values()].filter(p => p.state !== 'disconnected');
    const exact = outs.find(o => o.name === input.name);
    if (exact) return exact;
    const same = outs.filter(o => baseName(o.name || '') === baseName(input.name || ''));
    if (same.length === 1) return same[0];
    return outs.length === 1 ? outs[0] : null;
  }

  /** le premier port qui répond à la demande d'identité (et à want(identité)) → { link, id, name } */
  async find(want) {
    for (const input of this.candidates()) {
      const output = this.pairOutput(input);
      if (!output) continue;
      const link = new Link(this.access, input, output);
      try { await link.open(); } catch (e) { link.close(); continue; }
      const id = await handshake(link, 2);
      if (id && (!want || want(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(want, ms) {
    const end = now() + ms;
    while (now() < end) {
      const dev = await this.find(want);
      if (dev) return dev;
      await wait(DELAY.retry);
    }
    return null;
  }

  async serve(link, image, finish, idle, progress) {
    let served = 0, last = now();
    for (;;) {
      const pkt = await link.read(DELAY.poll);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (now() - last > idle) return { served, finished: false, lost: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = now();
      if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
        link.send(response(r.addr, [0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0]));   // "success\0"
        if (r.addr === finish) return { served, finished: true, lost: false };
        continue;
      }
      if (r.n > MAXDATA || r.addr + r.n > image.length)
        throw new InstallError('badreq', 'le FM-1 a demandé 0x' + r.addr.toString(16) + '+' + r.n + ', hors du paquet');
      if (DELAY.reply) await wait(DELAY.reply);
      if (!link.send(response(r.addr, image.subarray(r.addr, r.addr + r.n), r.fl))) return { served, finished: false, lost: true };
      served++;
      progress(served, r.addr + r.n);
    }
  }

  async check(dev, image, step) {
    step('start', dev.id.text);
    dev.link.send(UPGRADE);
    await wait(DELAY.start);
    const r = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idle_check, k => step('check', k));
    dev.link.close();
    if (!r.finished)
      throw new InstallError(r.lost ? 'lost' : 'stopped', 'le FM-1 ' + (r.lost ? 'a été déconnecté' : 'ne répond plus')
        + ' après ' + r.served + ' demandes ; rien n’a été écrit.');
    step('loader');
    await wait(DELAY.loader);
    const ota = await this.waitFor(i => i.loader, DELAY.wait_loader);
    if (!ota)
      throw new InstallError('noloader', 'le chargeur de mise à jour n’est pas apparu. Débranche et rebranche le câble USB, '
        + 'puis relance l’installation : le FM-1 reste en mode mise à jour tant qu’elle n’est pas finie.');
    return ota;
  }

  async write(ota, image, step) {
    step('write', 0);
    ota.link.send(UPGRADE);
    await wait(DELAY.start);
    const r = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idle_write,
      (k, end) => step('write', Math.min(99, Math.floor(end * 100 / image.length))));
    ota.link.close();
    if (!r.finished)
      throw new InstallError(r.lost ? 'lost' : 'stopped', 'le chargeur ' + (r.lost ? 'a été déconnecté' : 'ne répond plus')
        + ' après ' + r.served + ' demandes. Rebranche le câble USB et relance l’installation pour la terminer.');
    step('write', 100);
  }

  async verify(product, step) {
    step('reboot');
    await wait(DELAY.reboot);
    const back = await this.waitFor(i => !i.loader, DELAY.wait_reboot);
    if (!back) throw new InstallError('noreturn', 'le FM-1 n’est pas revenu : éteins-le et rallume-le.');
    back.link.close();
    if (back.id.text !== product)
      throw new InstallError('mismatch', 'écrit, mais le FM-1 répond ' + back.id.text + ' (attendu : ' + product + ').');
    step('done', back.id.text);
    return back.id.text;
  }

  async install(dev, image, product, step = () => {}) {
    if (!sameKeyboard(dev.id.text, product)) {
      dev.link.close();
      throw new InstallError('model', 'l’appareil est un ' + dev.id.text + ', le paquet est pour ' + product + '.');
    }
    if (dev.id.loader && !OWN_LOADER_RE.test(dev.id.text)) {
      dev.link.close();
      throw new InstallError('foreign', 'le FM-1 est en mode mise à jour d’un autre firmware (' + dev.id.text
        + ') : termine cette mise à jour avec son propre outil, puis recommence.');
    }
    const ota = dev.id.loader ? dev : await this.check(dev, image, step);
    await this.write(ota, image, step);
    return this.verify(product, step);
  }
}

const API = { DELAY, InstallError, pack7, unpack7, response, parseRequest, parseIdentity, productOf, blockCount,
  logicalImage, deinterleave, packageDamage, crc16, dec, loadPackage, sameKeyboard, Updater, HS_QUERY, UPGRADE,
  FINISH_CHECK, FINISH_WRITE, LOADER_MARK, KEEP };
if (typeof module !== 'undefined' && module.exports) module.exports = API; else root.FM1OTA = API;
})(typeof self !== 'undefined' ? self : this);
