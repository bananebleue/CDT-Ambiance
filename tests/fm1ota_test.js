// SPDX-License-Identifier: GPL-3.0-only
// fm1ota.js contre un FM-1 simulé derrière une fausse interface Web MIDI (pas de matériel).
// Même appareil simulé que tests/install_test.py de Dinghy : identité à la demande, puis lectures du paquet.
//   node tests/fm1ota_test.js
'use strict';
const O = require('../fm1ota.js');
Object.assign(O.DELAY, { open: 0, start: 10, reply: 0, loader: 10, reboot: 10, retry: 20, poll: 50, hs: 100,
  idle_check: 400, idle_write: 400, wait_loader: 2000, wait_reboot: 2000 });

let failed = 0;
const ok = (c, what) => { console.log(what.padEnd(64) + (c ? ' ok' : ' FAIL')); if (!c) failed++; };
const bytes = s => [...s].map(c => c.charCodeAt(0));

/* ------------------------------------------------------- paquet synthétique --- */
function pkg(product = 'FM-1_960', { marker = true, size = 0x2000, damage = false, seed = 7 } = {}) {
  const img = new Uint8Array(size);
  for (let i = 0; i < size; i++) img[i] = (i * seed) & 0xFF;
  img.set(marker ? O.LOADER_MARK : new Array(16).fill(0), 0x1800);
  const data = img.subarray(0x400);
  const e = new Uint8Array(0x50);
  const c = O.crc16(data);
  e[4] = c & 0xFF; e[5] = c >> 8;
  e[8] = 0x00; e[9] = 0x04;
  e[12] = data.length & 0xFF; e[13] = (data.length >> 8) & 0xFF; e[14] = (data.length >> 16) & 0xFF;
  e.set(bytes('flash.bin'), 0x40);
  const lst = O.dec(e);
  const hdr = new Uint8Array(0x40);
  hdr[4] = size & 0xFF; hdr[5] = (size >> 8) & 0xFF; hdr[6] = (size >> 16) & 0xFF;
  hdr[8] = 1;
  const lc = O.crc16(lst); hdr[2] = lc & 0xFF; hdr[3] = lc >> 8;
  const hc = O.crc16(hdr.subarray(2)); hdr[0] = hc & 0xFF; hdr[1] = hc >> 8;
  img.set(O.dec(hdr), 0); img.set(lst, 0x40);
  if (damage) img[0x1000] ^= 1;
  const raw = [];
  for (let i = 0; i < 20; i++) {
    raw.push(...img.subarray(i * O.KEEP, (i + 1) * O.KEEP));
    raw.push(i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D);
  }
  raw.push(...img.subarray(20 * O.KEEP));
  return { raw: Uint8Array.from(raw), img };
}

/* --------------------------------------------- fausse interface Web MIDI + FM-1 --- */
class Port {
  constructor(dev, type, name, id) { this.dev = dev; this.type = type; this.name = name; this.id = id; this.state = 'connected'; this.connection = 'closed'; this.ls = []; }
  open() { this.connection = 'open'; return Promise.resolve(this); }
  close() { this.connection = 'closed'; return Promise.resolve(this); }
  addEventListener(_t, f) { this.ls.push(f); }
  removeEventListener(_t, f) { this.ls = this.ls.filter(x => x !== f); }
  send(d) { if (this.state !== 'connected') throw new Error('disconnected'); this.dev.sent++; this.dev.rx(Array.from(d)); }
}

class FakeFM1 {
  constructor(image, o = {}) {
    this.image = image;
    this.o = Object.assign({ identity: 'FM-1_015', name: 'FM-1', after: 'FM-1_960', loader: ['ota-FM-1_900', 'Felucca Update'] }, o);
    this.access = { inputs: new Map(), outputs: new Map() };
    this.gen = 0; this.served = 0; this.bad = 0; this.sent = 0; this.upgrades = 0;
    this.other();
    this.boot(this.o.identity, this.o.name);
  }
  other() {    // un autre appareil MIDI branché : au plus la demande d'identité, quand aucun port n'a un nom de FM-1
    const i = new Port(this, 'input', 'Clavier Arturia', 'x-in'), q = new Port(this, 'output', 'Clavier Arturia', 'x-out');
    this.foreignSent = [];
    q.send = d => { this.foreign = true; this.foreignSent.push(Array.from(d)); };
    this.access.inputs.set(i.id, i); this.access.outputs.set(q.id, q);
  }
  boot(identity, name) {
    for (const m of [this.access.inputs, this.access.outputs])
      for (const [k, p] of m) if (p.dev === this && !k.startsWith('x-')) { p.state = 'disconnected'; m.delete(k); }
    this.gen++;
    this.identity = identity; this.waiting = null; this.queue = [];
    this.in = new Port(this, 'input', name, 'in' + this.gen);
    this.out = new Port(this, 'output', name, 'out' + this.gen);
    this.access.inputs.set(this.in.id, this.in); this.access.outputs.set(this.out.id, this.out);
  }
  unplug() { this.in.state = this.out.state = 'disconnected'; this.access.inputs.delete(this.in.id); this.access.outputs.delete(this.out.id); }
  tx(pkt) { const p = this.in; setTimeout(() => p.ls.forEach(f => f({ data: Uint8Array.from(pkt) })), 1); }
  rx(d) {
    const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
    if (eq(d, O.HS_QUERY)) {
      const t = bytes(this.identity);
      this.tx([0xF0, ...O.pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (eq(d, O.UPGRADE)) {
      this.upgrades++;
      const first = this.o.badAddr !== undefined ? this.o.badAddr : 0;
      this.queue = this.identity.startsWith('ota-')
        ? [...Array(6).keys()].map(k => [first + k * 512, 512]).concat([[O.FINISH_WRITE, 8]])
        : [[0, 64], [0x40, 160], [0x1000, 512], [O.FINISH_CHECK, 8]];
      this.next();
    } else if (this.waiting) {
      const u = O.unpack7(d.slice(1, -1));
      const [addr, n] = this.waiting;
      const want = addr >= O.FINISH_CHECK ? bytes('success\0') : Array.from(this.image.subarray(addr, addr + n));
      if (u.length !== 15 + want.length || want.some((b, i) => u[14 + i] !== b)) this.bad++;
      this.waiting = null; this.served++;
      if (this.o.unplugAfter && this.served >= this.o.unplugAfter) { this.unplug(); return; }
      if (this.o.stallAfter && this.served >= this.o.stallAfter) return;
      if (addr === O.FINISH_CHECK) setTimeout(() => this.boot(...this.o.loader), 50);
      else if (addr === O.FINISH_WRITE) setTimeout(() => this.boot(this.o.after, 'Felucca'), 50);
      else this.next();
    }
  }
  next() {
    if (!this.queue.length) return;
    const [addr, n] = this.waiting = this.queue.shift();
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, n & 0xFF, n >> 8, 0];
    let s = 0; for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...O.pack7(u), 0xF7]);
  }
}

async function install(dev, raw, steps = []) {
  const { product, image } = O.loadPackage(raw);
  const up = new O.Updater(dev.access);
  const d = await up.find();
  if (!d) throw new O.InstallError('notfound', 'FM-1 introuvable');
  return up.install(d, image, product, (k, a) => steps.push(k));
}

async function main() {
  // format filaire
  const data = Array.from({ length: 768 }, (_, i) => i & 0xFF);
  ok(O.unpack7(O.pack7(data)).slice(0, 768).every((b, i) => b === data[i]), 'pack7 / unpack7 aller-retour');
  ok(O.pack7([0, 0x59, 0x11]).slice(0, 3).join() === [0x00, 0x32, 0x45].join(), 'identité : F0 00 32 45 = en-tête M-VAVE');
  const r = O.unpack7(O.response(0x12345, [1, 2, 3], 5).slice(1, -1));
  let s = 0; for (let i = 6; i < 17; i++) s += r[i];
  ok(r[0] === 0 && r[1] === 0x59 && r[2] === 0x30 && (r[7] | r[8] << 8 | r[9] << 16) === 0x12345 && r[14] === 1 && (~s & 0xFF) === r[17],
    'réponse : en-tête, adresse, données, somme');

  // paquet
  const { raw, img } = pkg();
  const L = O.loadPackage(raw);
  ok(L.product === 'FM-1_960', 'identité du paquet : FM-1_960');
  ok(L.image.length === img.length && L.image.every((b, i) => b === img[i]), 'image logique = image d’origine');
  const thrown = (f, code) => { try { f(); return false; } catch (e) { return e.code === code; } };
  ok(thrown(() => O.loadPackage(pkg('FM-1_960', { damage: true }).raw), 'badpkg'), 'paquet abîmé refusé');
  ok(thrown(() => O.loadPackage(pkg('FM-1_960', { marker: false }).raw), 'badpkg'), 'paquet sans chargeur Felucca refusé');
  ok(thrown(() => O.loadPackage(raw.subarray(0, 100)), 'badpkg'), 'fichier trop court refusé');

  // installation complète
  let dev = new FakeFM1(img);
  const steps = [];
  const got = await install(dev, raw, steps);
  ok(got === 'FM-1_960', 'installation : le FM-1 répond FM-1_960');
  ok(dev.bad === 0 && dev.served === 4 + 7 && dev.upgrades === 2, 'toutes les réponses justes (4 + 7 lectures, 2 démarrages)');
  ok(['start', 'check', 'loader', 'write', 'reboot', 'done'].every(k => steps.includes(k)), 'étapes annoncées dans l’ordre prévu');
  ok(dev.foreignSent.every(d => d.join() === O.HS_QUERY.join()), 'l’autre appareil MIDI ne reçoit au plus que la demande d’identité');

  // FM-1 déjà en mode mise à jour (installation coupée) : on finit l'écriture
  dev = new FakeFM1(img, { identity: 'ota-FM-1_900', name: 'Felucca Update' });
  ok(await install(dev, raw) === 'FM-1_960' && dev.upgrades === 1, 'reprise d’une écriture interrompue');

  const fails = async (o, code, what, p = raw) => {
    const d = new FakeFM1(img, o);
    try { await install(d, p); ok(false, what); } catch (e) { ok(e.code === code, what + ' (' + e.code + ')'); }
  };
  await fails({ identity: 'SMK-37 Pro_016', name: 'SMK-37 Pro' }, 'model', 'mauvais modèle refusé');
  await fails({ identity: 'ota-FM-1_015', name: 'FM-1 ota' }, 'foreign', 'mode mise à jour d’un autre firmware refusé');
  await fails({ unplugAfter: 2 }, 'lost', 'câble débranché pendant la vérification');
  await fails({ unplugAfter: 7 }, 'lost', 'câble débranché pendant l’écriture');
  await fails({ stallAfter: 2 }, 'stopped', 'FM-1 muet pendant la vérification');
  await fails({ after: 'FM-1_015' }, 'mismatch', 'autre identité après redémarrage signalée');
  await fails({ badAddr: 0x1F00 }, 'badreq', 'demande hors du paquet refusée');

  // firmware qui donne à son port un nom à lui (FM1-Radio : « FIELD ») : trouvé quand même
  dev = new FakeFM1(img, { identity: 'FM-1_13010300', name: 'FIELD' });
  ok(await install(dev, raw) === 'FM-1_960', 'port au nom inconnu (« FIELD ») : trouvé et installé');
  const hs = O.HS_QUERY.join();
  ok(dev.foreignSent.every(d => d.join() === hs), 'l’autre appareil MIDI ne reçoit que la demande d’identité');

  const d0 = new FakeFM1(img); d0.unplug();
  ok(await new O.Updater(d0.access).find() === null, 'aucun FM-1 : find() rend null');

  console.log(failed ? failed + ' échec(s)' : 'tout est bon');
  process.exit(failed ? 1 : 0);
}
main().catch(e => { console.error(e); process.exit(1); });
