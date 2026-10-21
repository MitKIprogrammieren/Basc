'use strict';
/* Nachbildung von window.fl (preload.js) für die Browser-Vorschau – nutzt das echte Datenmodell */
(function () {
  const m = window.__m.modell;
  const hoerer = {};
  const melden = (k, d) => (hoerer[k] || []).forEach((f) => f(d));
  let sorten = m.lagerAusAntwort(window.__testdaten, 'todo.filament');
  const drucker = window.__m.demo.demoDrucker(sorten);
  let regale = {
    regale: [{ id: 'r1', name: 'Regal Keller', unter: 'links an der Wand', ebenen: 4, plaetze: 6 }, { id: 'r2', name: 'Trockenbox', unter: '', ebenen: 1, plaetze: 4 }],
    plaetze: {},
  };
  sorten.slice(0, 15).forEach((s, i) => { regale.plaetze[`r1/${Math.floor(i / 6)}/${i % 6}`] = { sorte: s.uid }; });
  regale.plaetze['r2/0/1'] = { sorte: sorten[20].uid };
  let einst = {
    ha: { url: 'http://homeassistant.local:8123', todo: 'todo.filament', zertifikatPruefen: true, tokenGesetzt: false },
    waage: { art: 'demo', adresse: 'filamentwaage.local', port: 8080, leerspule_g: 250 }, drucker: { namen: {}, ausgeblendet: [] },
    etikett: { breite_mm: 50, hoehe_mm: 30, dpi: 300, druckbreite_punkte: 576, dichte: 8, qr: true, felder: ['hersteller', 'temperatur', 'trocknen'] }, demo: true,
  };
  const lager = () => ({ sorten, version: 1, ok: true, grund: '', stand: Date.now(), demo: true });
  // Waage: feste Szene über ?szene=leer|bekannt|unbekannt, sonst Wechsel alle 6 s
  const szene = new URLSearchParams(location.search).get('szene');
  const SZ = { leer: { g: 0, R: '' }, bekannt: { g: 812.4, R: '04C0FFEE112233' }, unbekannt: { g: 1130, R: '04DEADBEEF0001' } };
  let w = { gramm: 0, stabil: true, kalibriert: true, tagL: '', tagR: '', verbunden: true };
  setInterval(() => {
    const sz = SZ[szene] || [SZ.leer, SZ.bekannt, SZ.unbekannt][Math.floor(Date.now() / 6000) % 3];
    w = { ...w, gramm: sz.g, tagR: sz.R, stabil: true };
    melden('waage', w);
  }, 300);
  const aendern = (uid, fn) => {
    const s = sorten.find((x) => x.uid === uid);
    const a = fn(s.beschreibung);
    if (a.geaendert) sorten = sorten.map((x) => (x.uid === uid ? m.sorteAusEintrag(x.uid, x.summary, a.beschreibung) : x));
    setTimeout(() => melden('lager', lager()), 50);
    return a.meldung;
  };
  window.fl = {
    app_start: async () => ({ einst, version: '1.0.0', demo: true, plattform: 'darwin' }),
    einst_speichern: async (d) => {
      einst = { ...einst, ...d, ha: { ...einst.ha, ...(d.ha || {}) } };
      if (d.drucker) melden('drucker', { liste: drucker, ausgeblendet: einst.drucker.ausgeblendet });
      return einst;
    },
    ha_testen: async () => { throw new Error('In der Vorschau gibt es kein Home Assistant'); },
    lager_holen: async () => lager(),
    lager_neu: async () => lager(),
    lager_aendern: async (d) => {
      if (d.op === 'rest') return aendern(d.sorte, (b) => m.restSpeichern(b, Math.round(d.netto), d.tag || ''));
      if (d.op === 'spulen') return aendern(d.sorte, (b) => m.spulenAendern(b, d.delta));
      if (d.op === 'restStreichen') return aendern(d.sorte, (b) => m.restStreichen(b, d.gramm));
      if (d.op === 'leer') return aendern(d.sorte, (b) => m.leerSetzen(b, d.gramm));
      if (d.op === 'tagEntfernen') return aendern(d.sorte, (b) => m.tagEntfernen(b, d.tag));
      if (d.op === 'zuordnen') return aendern(d.sorte, (b) => m.tagZuordnen(b, d.tag));
      throw new Error('unbekannt');
    },
    drucker_holen: async () => ({ liste: drucker, ausgeblendet: einst.drucker.ausgeblendet }),
    drucker_neu: async () => {},
    drucker_filament: async (d) => `${sorten.find((s) => s.uid === d.sorte).kurzname} eingetragen`,
    drucker_rfid: async () => 'AMS liest den Tag neu',
    drucker_bild: async () => null,
    waage_tara: async () => melden('meldung', { text: 'Tara gesetzt', fehler: false }),
    waage_kal: async () => {},
    waage_holen: async () => w,
    regale_holen: async () => regale,
    regale_speichern: async (d) => { regale = d; return true; },
    regale_export: async () => true,
    regale_import: async () => null,
    etikett_png: async () => true,
    etikett_drucken: async () => true,
    extern_oeffnen: async () => {},
    bt_waehlen: async () => {},
    auf: (k, f) => { (hoerer[k] = hoerer[k] || []).push(f); },
  };
})();
