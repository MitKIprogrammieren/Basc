'use strict';
/**
 * FilamentLager Studio – Mac-App zur FilamentLager-Waage.
 * Hauptprozess: Einstellungen, Home Assistant, Waage, Drucker, Regale, Etiketten.
 */
const { app, BrowserWindow, ipcMain, dialog, safeStorage, shell, nativeTheme } = require('electron');
const path = require('node:path');
const fs = require('node:fs');

const modell = require('./services/modell');
const { Ha, HaFehler } = require('./services/ha');
const { Waage } = require('./services/waage');
const drucker = require('./services/drucker');
const { demoDrucker } = require('./services/demo');

const VERSION = require('./package.json').version;
const DEMO_FLAG = process.argv.includes('--demo');

// ------------------------------------------------------------------ Dateien
const daten = (name) => path.join(app.getPath('userData'), name);
function jsonLesen(datei, standard) {
  try { return JSON.parse(fs.readFileSync(datei, 'utf8')); } catch { return standard; }
}
function jsonSchreiben(datei, wert) {
  fs.mkdirSync(path.dirname(datei), { recursive: true });
  const tmp = datei + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(wert, null, 2));
  fs.renameSync(tmp, datei);
}
function testdatenPfad() {
  const k = [path.join(process.resourcesPath || '', 'testdaten', 'todo_filament.json'),
    path.join(__dirname, '..', 'testdaten', 'todo_filament.json')];
  return k.find((p) => fs.existsSync(p)) || k[1];
}

// ------------------------------------------------------------------ Einstellungen
const STANDARD = {
  ha: { url: 'http://homeassistant.local:8123', todo: 'todo.filament', zertifikatPruefen: true },
  waage: { art: 'pi', adresse: 'filamentwaage.local', port: 8080, leerspule_g: 250 },
  drucker: { namen: {}, ausgeblendet: [] },
  etikett: { breite_mm: 50, hoehe_mm: 30, dpi: 300, druckbreite_punkte: 576, dichte: 8, qr: true, felder: ['hersteller', 'temperatur', 'trocknen'] },
  demo: false,
};
let einst = mischen(STANDARD, jsonLesen(daten('einstellungen.json'), {}));
function mischen(a, b) {
  const o = { ...a };
  for (const [k, v] of Object.entries(b || {})) o[k] = v && typeof v === 'object' && !Array.isArray(v) && a[k] && typeof a[k] === 'object' ? mischen(a[k], v) : v;
  return o;
}
function token() {
  const t = einst.ha.tokenVerschluesselt;
  if (t && safeStorage.isEncryptionAvailable()) {
    try { return safeStorage.decryptString(Buffer.from(t, 'base64')); } catch { return ''; }
  }
  return einst.ha.tokenKlar || '';
}
function tokenSetzen(t) {
  delete einst.ha.tokenVerschluesselt;
  delete einst.ha.tokenKlar;
  if (!t) return;
  if (safeStorage.isEncryptionAvailable()) einst.ha.tokenVerschluesselt = safeStorage.encryptString(t).toString('base64');
  else einst.ha.tokenKlar = t;
}
const demoAktiv = () => DEMO_FLAG || einst.demo || !token();
function einstFuerOberflaeche() {
  const e = JSON.parse(JSON.stringify(einst));
  delete e.ha.tokenVerschluesselt;
  delete e.ha.tokenKlar;
  e.ha.tokenGesetzt = !!token();
  return e;
}

// ------------------------------------------------------------------ Zustand
const ha = new Ha();
const waage = new Waage();
let fenster = null;
const lager = { sorten: [], version: 0, ok: false, grund: '', stand: 0 };
const dr = { karte: null, zustaende: new Map(), liste: [], demo: null };
let auftraege = Promise.resolve();

function senden(kanal, wert) {
  if (fenster && !fenster.isDestroyed()) fenster.webContents.send(kanal, wert);
}
function meldung(text, ok = true) { senden('meldung', { text, fehler: !ok }); }

// ------------------------------------------------------------------ Lager
function lagerSetzen(sorten) {
  lager.sorten = sorten;
  lager.version += 1;
  lager.ok = true;
  lager.grund = '';
  lager.stand = Date.now();
  senden('lager', lagerFuerOberflaeche());
  druckerSenden();
}
function lagerFuerOberflaeche() {
  return { sorten: lager.sorten, version: lager.version, ok: lager.ok, grund: lager.grund, stand: lager.stand, demo: demoAktiv() };
}
async function lagerFrisch() {
  if (demoAktiv()) return lager.sorten;
  return modell.lagerAusItems(await ha.todoEintraege(einst.ha.todo));
}
async function lagerLaden() {
  try {
    if (demoAktiv()) {
      if (!lager.sorten.length) lagerSetzen(modell.lagerAusAntwort(jsonLesen(testdatenPfad(), {}), 'todo.filament'));
      return;
    }
    lagerSetzen(await lagerFrisch());
  } catch (e) {
    const war = lager.ok;
    lager.ok = false;
    lager.grund = e.message;
    senden('lager', lagerFuerOberflaeche());
    if (war || !lager.version) meldung(e.message, false);
  }
}

/** Eine Sorte ändern: frischen Stand holen, Regel anwenden, zurückschreiben. */
async function sorteAendern(uid, fn) {
  const frisch = await lagerFrisch();
  const s = modell.findeUid(frisch, uid);
  if (!s) throw new HaFehler('Sorte nicht mehr im Lager');
  const a = fn(s.beschreibung);
  if (a.geaendert) {
    if (demoAktiv()) {
      lager.sorten = lager.sorten.map((x) => (x.uid === uid ? modell.sorteAusEintrag(x.uid, x.summary, a.beschreibung) : x));
    } else {
      await ha.todoBeschreibung(einst.ha.todo, uid, a.beschreibung);
    }
  }
  return a.meldung;
}

function lagerAuftrag(d) {
  const lauf = async () => {
    let text = '';
    if (d.op === 'rest') text = await sorteAendern(d.sorte, (b) => modell.restSpeichern(b, Math.round(d.netto), d.tag || ''));
    else if (d.op === 'spulen') text = await sorteAendern(d.sorte, (b) => modell.spulenAendern(b, Math.trunc(d.delta)));
    else if (d.op === 'restStreichen') text = await sorteAendern(d.sorte, (b) => modell.restStreichen(b, Math.round(d.gramm)));
    else if (d.op === 'leer') text = await sorteAendern(d.sorte, (b) => modell.leerSetzen(b, d.gramm));
    else if (d.op === 'tagEntfernen') text = await sorteAendern(d.sorte, (b) => modell.tagEntfernen(b, d.tag));
    else if (d.op === 'zuordnen') {
      const andere = lager.sorten.filter((s) => s.nfc.includes(d.tag) && s.uid !== d.sorte).map((s) => s.uid);
      for (const uid of andere) await sorteAendern(uid, (b) => modell.tagEntfernen(b, d.tag));
      text = await sorteAendern(d.sorte, (b) => modell.tagZuordnen(b, d.tag));
    } else throw new Error('unbekannte Änderung');
    if (demoAktiv()) lagerSetzen(lager.sorten);
    else await lagerLaden();
    return text;
  };
  const p = auftraege.then(lauf, lauf);
  auftraege = p.catch(() => {});
  return p;
}

// ------------------------------------------------------------------ Drucker
let druckerTimer = null;
function druckerSenden() {
  if (druckerTimer) return;
  druckerTimer = setTimeout(() => {
    druckerTimer = null;
    let liste;
    if (demoAktiv()) {
      if (!dr.demo && lager.sorten.length) dr.demo = demoDrucker(lager.sorten);
      liste = dr.demo || [];
      for (const d of liste) if (einst.drucker.namen[d.id]) d.name = einst.drucker.namen[d.id];
    } else {
      liste = dr.karte ? drucker.druckerListe(dr.karte, dr.zustaende, einst.drucker.namen) : [];
    }
    dr.liste = liste;
    senden('drucker', { liste, ausgeblendet: einst.drucker.ausgeblendet });
  }, 300);
}
async function druckerKarteLaden() {
  try {
    const [ent, ger, zst] = await Promise.all([ha.entitaeten(), ha.geraete(), ha.zustaende()]);
    dr.karte = drucker.karteAusRegistry(ent, ger);
    dr.zustaende = new Map(zst.filter((s) => dr.karte.rollen.has(s.entity_id)).map((s) => [s.entity_id, s]));
    druckerSenden();
  } catch (e) {
    meldung(`Drucker: ${e.message}`, false);
  }
}
ha.on('ereignis', (ev) => {
  if (ev.event_type === 'state_changed') {
    const id = ev.data?.entity_id;
    if (dr.karte?.rollen.has(id)) {
      if (ev.data.new_state) dr.zustaende.set(id, ev.data.new_state);
      druckerSenden();
    }
    if (id === einst.ha.todo) lagerLaden();
  } else if (ev.event_type === 'entity_registry_updated') {
    clearTimeout(dr.registryTimer);
    dr.registryTimer = setTimeout(druckerKarteLaden, 3000);
  }
});
ha.on('bereit', async () => {
  try {
    await ha.abonnieren('state_changed');
    await ha.abonnieren('entity_registry_updated');
  } catch { /* nur Live-Updates fehlen */ }
  await lagerLaden();
  await druckerKarteLaden();
});
ha.on('status', (s) => {
  senden('ha-status', { ...s, demo: demoAktiv() });
  if (!s.verbunden && s.grund) { lager.ok = false; lager.grund = s.grund; senden('lager', lagerFuerOberflaeche()); }
});

// ------------------------------------------------------------------ Waage
let waageTakt = 0;
waage.on('wert', (w) => {
  const jetzt = Date.now();
  if (jetzt - waageTakt < 90 && w.verbunden) return;
  waageTakt = jetzt;
  senden('waage', w);
});
waage.on('meldung', (m) => senden('meldung', m));

// ------------------------------------------------------------------ Konfiguration anwenden
function anwenden() {
  if (demoAktiv()) {
    ha.beenden();
    lager.sorten = [];
    dr.demo = null;
    lagerLaden();
    senden('ha-status', { verbunden: false, grund: '', demo: true });
  } else {
    dr.demo = null;
    ha.konfigurieren({ url: einst.ha.url, token: token(), zertifikatPruefen: einst.ha.zertifikatPruefen });
  }
  const w = einst.waage;
  const waageDemo = DEMO_FLAG || (demoAktiv() && !w.eingerichtet);
  waage.konfigurieren(waageDemo ? { art: 'demo' } : { art: w.art, adresse: w.adresse, port: w.port });
}

// ------------------------------------------------------------------ IPC
const ok = (fn) => async (_e, d) => {
  try { return { ok: true, wert: await fn(d || {}) }; } catch (e) { return { ok: false, fehler: e.message }; }
};

ipcMain.handle('app:start', ok(() => ({ einst: einstFuerOberflaeche(), version: VERSION, demo: demoAktiv(), plattform: process.platform })));
ipcMain.handle('einst:speichern', ok((d) => {
  const vorher = JSON.stringify([einst.ha, einst.waage, einst.demo, token()]);
  if (typeof d.token === 'string') tokenSetzen(d.token.trim());
  delete d.token;
  if (d.waage) d.waage.eingerichtet = true;
  einst = mischen(einst, d);
  if (d.drucker?.namen) einst.drucker.namen = d.drucker.namen; // gelöschte Namen nicht zurückmischen
  jsonSchreiben(daten('einstellungen.json'), einst);
  if (JSON.stringify([einst.ha, einst.waage, einst.demo, token()]) !== vorher) anwenden();
  else if (d.drucker) druckerSenden();
  return einstFuerOberflaeche();
}));
ipcMain.handle('ha:testen', ok(async (d) => {
  const t = new Ha();
  return new Promise((ja, nein) => {
    const ende = setTimeout(() => { t.beenden(); nein(new Error('keine Antwort von Home Assistant')); }, 10000);
    t.on('status', (s) => { if (!s.verbunden && s.grund) { clearTimeout(ende); t.beenden(); nein(new Error(s.grund)); } });
    t.on('bereit', async () => {
      try {
        const items = await t.todoEintraege(d.todo || 'todo.filament');
        clearTimeout(ende); t.beenden();
        ja(`Verbunden – ${modell.lagerAusItems(items).length} Sorten in ${d.todo}`);
      } catch (e) { clearTimeout(ende); t.beenden(); nein(e); }
    });
    t.konfigurieren({ url: d.url, token: d.token || token(), zertifikatPruefen: d.zertifikatPruefen !== false });
  });
}));

ipcMain.handle('lager:holen', ok(() => lagerFuerOberflaeche()));
ipcMain.handle('lager:neu', ok(async () => { if (demoAktiv()) lager.sorten = []; await lagerLaden(); return lagerFuerOberflaeche(); }));
ipcMain.handle('lager:aendern', ok((d) => lagerAuftrag(d)));

ipcMain.handle('drucker:holen', ok(() => ({ liste: dr.liste, ausgeblendet: einst.drucker.ausgeblendet })));
ipcMain.handle('drucker:neu', ok(async () => { if (!demoAktiv()) await druckerKarteLaden(); druckerSenden(); }));
ipcMain.handle('drucker:filament', ok(async (d) => {
  const s = modell.findeUid(lager.sorten, d.sorte);
  if (!s) throw new Error('Sorte nicht gefunden');
  const f = drucker.filamentFuerSlot(s);
  if (demoAktiv()) {
    for (const p of dr.liste) for (const a of p.ams) for (const sl of a.slots) if (sl.entity === d.entity) {
      Object.assign(sl, { leer: false, name: s.kurzname, typ: f.tray_type, hex: s.hex, filament_id: f.tray_info_idx, rest: -1 });
    }
    druckerSenden();
  } else {
    await ha.dienst('bambu_lab', 'set_filament', { entity_id: d.entity, ...f });
  }
  return `${s.kurzname} im Slot eingetragen (${f.tray_info_idx})`;
}));
ipcMain.handle('drucker:rfid', ok(async (d) => {
  if (!demoAktiv()) await ha.dienst('bambu_lab', 'read_rfid', { entity_id: d.entity });
  return 'AMS liest den Tag neu';
}));
ipcMain.handle('drucker:bild', ok((d) => ha.bild(d.pfad)));

ipcMain.handle('waage:tara', ok(() => waage.tara()));
ipcMain.handle('waage:kal', ok((d) => waage.kalibrieren(d.g || 1000)));
ipcMain.handle('waage:holen', ok(() => waage.wert));

ipcMain.handle('regale:holen', ok(() => jsonLesen(daten('regale.json'), { regale: [], plaetze: {} })));
ipcMain.handle('regale:speichern', ok((d) => { jsonSchreiben(daten('regale.json'), d); return true; }));
ipcMain.handle('regale:export', ok(async (d) => {
  const r = await dialog.showSaveDialog(fenster, { defaultPath: 'filamentlager-regale.json', filters: [{ name: 'JSON', extensions: ['json'] }] });
  if (r.canceled) return false;
  fs.writeFileSync(r.filePath, JSON.stringify(d, null, 2));
  return true;
}));
ipcMain.handle('regale:import', ok(async () => {
  const r = await dialog.showOpenDialog(fenster, { filters: [{ name: 'JSON', extensions: ['json'] }], properties: ['openFile'] });
  if (r.canceled || !r.filePaths[0]) return null;
  const d = JSON.parse(fs.readFileSync(r.filePaths[0], 'utf8'));
  if (!Array.isArray(d.regale) || typeof d.plaetze !== 'object') throw new Error('Keine Regal-Datei');
  return d;
}));

ipcMain.handle('etikett:png', ok(async (d) => {
  const r = await dialog.showSaveDialog(fenster, { defaultPath: d.name || 'etikett.png', filters: [{ name: 'PNG', extensions: ['png'] }] });
  if (r.canceled) return false;
  fs.writeFileSync(r.filePath, Buffer.from(d.dataUrl.split(',')[1], 'base64'));
  return true;
}));
ipcMain.handle('etikett:drucken', ok((d) => new Promise((ja, nein) => {
  // Druck über den Mac-Druckdialog – für Drucker mit macOS-Treiber
  const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
  const seiten = d.bilder.map((u) => `<div class="s"><img src="${u}"></div>`).join('');
  const html = `<!doctype html><style>@page{size:${d.breite_mm}mm ${d.hoehe_mm}mm;margin:0}html,body{margin:0}
    .s{width:${d.breite_mm}mm;height:${d.hoehe_mm}mm;page-break-after:always;overflow:hidden}img{width:100%;height:100%;display:block}</style>${seiten}`;
  w.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(html)).then(() => {
    w.webContents.print({ silent: false, printBackground: true, margins: { marginType: 'none' },
      pageSize: { width: Math.round(d.breite_mm * 1000), height: Math.round(d.hoehe_mm * 1000) } }, (erfolg, grund) => {
      w.destroy();
      if (erfolg || grund === 'cancelled') ja(erfolg); else nein(new Error(grund || 'Drucken fehlgeschlagen'));
    });
  }, (e) => { w.destroy(); nein(e); });
})));

ipcMain.handle('extern:oeffnen', ok((d) => {
  if (/^https?:\/\//.test(d.url)) shell.openExternal(d.url);
}));

// Bluetooth-Geräteauswahl (Web Bluetooth) – die Oberfläche zeigt die Liste
let btRueckruf = null;
ipcMain.handle('bt:waehlen', ok((d) => {
  if (btRueckruf) { btRueckruf(d.id || ''); btRueckruf = null; }
}));

// ------------------------------------------------------------------ Fenster
function fensterOeffnen() {
  nativeTheme.themeSource = 'dark';
  fenster = new BrowserWindow({
    width: 1320, height: 860, minWidth: 1040, minHeight: 680,
    backgroundColor: '#0d0f12', title: 'FilamentLager Studio',
    titleBarStyle: process.platform === 'darwin' ? 'hiddenInset' : 'default',
    trafficLightPosition: { x: 16, y: 18 },
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true },
  });
  fenster.webContents.on('select-bluetooth-device', (ev, geraete, rueckruf) => {
    ev.preventDefault();
    btRueckruf = rueckruf;
    senden('bt-geraete', geraete.map((g) => ({ id: g.deviceId, name: g.deviceName || 'Unbenannt' })));
  });
  fenster.webContents.session.setPermissionCheckHandler(() => true);
  fenster.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:\/\//.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  fenster.webContents.on('will-navigate', (e) => e.preventDefault());
  fenster.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  fenster.on('closed', () => { fenster = null; });
}

app.whenReady().then(() => {
  fensterOeffnen();
  anwenden();
  setInterval(() => { if (!demoAktiv() && ha.bereit) lagerLaden(); }, 60000);
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) fensterOeffnen(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('before-quit', () => { ha.beenden(); waage.konfigurieren({ art: 'aus' }); });
