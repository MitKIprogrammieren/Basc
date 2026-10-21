'use strict';
/**
 * Datenmodell für das Filamentlager „Ernsthaft gelagert“ (todo.filament).
 *
 * Dritte Fassung derselben Regeln wie pi/filamentwaage/modell.py und
 * firmware/display-5inch/src/model.cpp – Änderungen an allen Stellen nachziehen.
 * Jeder Todo-Eintrag ist eine Sorte, die Daten stehen als JSON in der Beschreibung.
 * Die Waage ergänzt nur "nfc", "waage" und optional "leer_g"; alle anderen Felder
 * (auch unbekannte) bleiben unverändert und in ihrer Reihenfolge.
 */

const LEER_GRENZE_G = 10;
const VOLL_TOLERANZ_G = 15;
const MESS_TOLERANZ_G = 5;

/** round() wie in Python (bei .5 zur geraden Zahl) */
function pyRound(x) {
  const f = Math.floor(x);
  const diff = x - f;
  if (diff > 0.5) return f + 1;
  if (diff < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}

function _int(v, standard) {
  if (typeof v === 'boolean') return standard;
  if (typeof v === 'number' && Number.isFinite(v)) return pyRound(v);
  if (typeof v === 'string' && v.trim()) {
    const n = Number(v.trim().replace(',', '.'));
    return Number.isFinite(n) ? Math.trunc(n) : standard;
  }
  return standard;
}

function _reste(v, gewicht) {
  const out = [];
  if (Array.isArray(v)) {
    for (const x of v) {
      const g = _int(x, -1);
      if (g > 0 && (gewicht <= 0 || g < gewicht)) out.push(g);
    }
  }
  return out;
}

function hexZuRgb(h, standard = '#888888') {
  return typeof h === 'string' && /^#[0-9a-fA-F]{6}$/.test(h) ? h.toLowerCase() : standard;
}

function _lesen(beschreibung) {
  if (!beschreibung) return {};
  try {
    const d = JSON.parse(beschreibung);
    return d && typeof d === 'object' && !Array.isArray(d) ? d : null;
  } catch {
    return null;
  }
}

const _schreiben = (d) => JSON.stringify(d);
const _strListe = (v) => (Array.isArray(v) ? v.filter((x) => typeof x === 'string') : []);
const _istDict = (v) => v && typeof v === 'object' && !Array.isArray(v);

function kurzname(s) {
  return [s.material, s.farbe].filter(Boolean).join(' ') || s.summary;
}

function bestandG(s) {
  return Math.max(0, s.spulen - s.reste.length) * s.gewicht + s.reste.reduce((a, b) => a + b, 0);
}

/** Gibt null zurück für Merkzeilen und Einstellungen der Karte. */
function sorteAusEintrag(uid, summary, beschreibung) {
  let d = _lesen(beschreibung);
  if (d === null) d = {};
  if (d.typ) return null;
  let gewicht = _int(d.gewicht, 1000);
  if (gewicht <= 0) gewicht = 1000;
  const spulen = Math.max(0, _int(d.spulen, 1));
  const s = {
    uid, summary, beschreibung: beschreibung || '',
    hersteller: String(d.hersteller || ''),
    material: String(d.material || ''),
    farbe: String(d.farbe || ''),
    hex: hexZuRgb(d.hex),
    besonderheiten: _strListe(d.besonderheiten),
    gewicht, spulen,
    refills: Math.max(0, Math.min(_int(d.refills, 0), spulen)),
    reste: _reste(d.reste, gewicht),
    duese_min: _int(d.duese_min, -1),
    duese_max: _int(d.duese_max, -1),
    bett_min: _int(d.bett_min, -1),
    bett_max: _int(d.bett_max, -1),
    trocknen_c: _int(d.trocknen_c, -1),
    trocknen_h: _int(d.trocknen_h, -1),
    durchmesser: typeof d.durchmesser === 'number' ? d.durchmesser : 1.75,
    leer_g: _int(d.leer_g, -1),
    fcode: String(d.fcode || ''),
    bambu_id: String(d.bambu_id || ''),
    codes: _strListe(d.codes),
    nfc: _strListe(d.nfc),
    waage: _istDict(d.waage) ? d.waage : {},
  };
  if (!s.material && !s.farbe) s.farbe = summary;
  s.kurzname = kurzname(s);
  s.bestand_g = bestandG(s);
  return s;
}

/** Einträge aus todo.get_items → Liste von Sorten (erledigte und Einstellungen fallen weg). */
function lagerAusItems(items) {
  if (!Array.isArray(items)) throw new Error('keine Einträge in der Antwort');
  const out = [];
  for (const it of items) {
    if (it.status === 'completed') continue;
    const s = sorteAusEintrag(it.uid || '', it.summary || '', it.description || '');
    if (s) out.push(s);
  }
  return out;
}

/** REST-Antwort von todo.get_items?return_response (wie in testdaten/todo_filament.json) */
function lagerAusAntwort(antwort, entity) {
  const items = antwort?.service_response?.[entity]?.items;
  if (items == null) throw new Error(`keine Einträge für ${entity} in der Antwort`);
  return lagerAusItems(items);
}

const findeNfc = (sorten, tag) => (tag ? sorten.find((s) => s.nfc.includes(tag)) || null : null);
const findeUid = (sorten, uid) => sorten.find((s) => s.uid === uid) || null;

const aenderung = (geaendert = false, beschreibung = '', meldung = '') => ({ geaendert, beschreibung, meldung });

function _naechster(reste, ziel) {
  if (!reste.length) return -1;
  let best = 0;
  for (let i = 1; i < reste.length; i++) if (Math.abs(reste[i] - ziel) < Math.abs(reste[best] - ziel)) best = i;
  return best;
}

function _gewichtSpulen(d) {
  let gewicht = _int(d.gewicht, 1000);
  if (gewicht <= 0) gewicht = 1000;
  return { gewicht, spulen: Math.max(0, _int(d.spulen, 1)) };
}

function restSpeichern(beschreibung, netto, tag = '') {
  const d = _lesen(beschreibung);
  if (d === null) return aenderung(false, '', 'Eintrag nicht lesbar');
  const { gewicht } = _gewichtSpulen(d);
  let { spulen } = _gewichtSpulen(d);
  const reste = _reste(d.reste, gewicht);
  const waage = _istDict(d.waage) ? d.waage : {};
  const alt = tag ? _int(waage[tag], -1) : -1;
  const a = aenderung();

  if (netto >= gewicht - VOLL_TOLERANZ_G) {
    if (alt > 0) {
      const i = _naechster(reste, alt);
      if (i >= 0) reste.splice(i, 1);
      delete waage[tag];
      a.geaendert = true; a.meldung = 'Spule ist voll – Rest gestrichen';
    } else {
      return aenderung(false, '', 'Spule ist voll – nichts geändert');
    }
  } else if (netto <= LEER_GRENZE_G) {
    let i = alt > 0 ? _naechster(reste, alt) : -1;
    if (i < 0 && reste.length) i = reste.indexOf(Math.min(...reste));
    if (i >= 0) reste.splice(i, 1);
    spulen = Math.max(0, spulen - 1);
    d.spulen = spulen;
    if (_int(d.refills, 0) > spulen) d.refills = spulen;
    if (tag) delete waage[tag];
    a.geaendert = true; a.meldung = 'Spule leer – ausgebucht';
  } else {
    let i = alt > 0 ? _naechster(reste, alt) : -1;
    if (i < 0) {
      let bester = -1;
      reste.forEach((r, k) => { if (r >= netto - MESS_TOLERANZ_G && (bester < 0 || r < reste[bester])) bester = k; });
      i = bester;
    }
    if (i < 0 && reste.length < spulen) {
      reste.push(netto);
      a.meldung = `Neue angebrochene Spule: ${netto} g`;
    } else {
      if (i < 0) i = _naechster(reste, netto);
      if (i < 0) {
        spulen += 1;
        d.spulen = spulen;
        reste.push(netto);
        a.meldung = `Spule ergänzt: ${netto} g`;
      } else {
        a.meldung = `Rest ${reste[i]} g → ${netto} g`;
        reste[i] = netto;
      }
    }
    if (tag) waage[tag] = netto;
    a.geaendert = true;
  }

  d.reste = reste;
  if (Object.keys(waage).length) d.waage = waage;
  else delete d.waage;
  a.beschreibung = _schreiben(d);
  return a;
}

function tagZuordnen(beschreibung, tag) {
  const d = _lesen(beschreibung);
  if (d === null || !tag) return aenderung(false, '', 'Eintrag nicht lesbar');
  const nfc = Array.isArray(d.nfc) ? d.nfc : [];
  if (nfc.includes(tag)) return aenderung(false, '', 'Tag war schon zugeordnet');
  d.nfc = [...nfc, tag];
  return aenderung(true, _schreiben(d), 'Tag gespeichert');
}

function tagEntfernen(beschreibung, tag) {
  const d = _lesen(beschreibung);
  if (d === null) return aenderung(false, '', 'Eintrag nicht lesbar');
  let nfc = Array.isArray(d.nfc) ? d.nfc : [];
  if (!nfc.includes(tag)) return aenderung();
  nfc = nfc.filter((x) => x !== tag);
  if (nfc.length) d.nfc = nfc;
  else delete d.nfc;
  if (_istDict(d.waage)) {
    delete d.waage[tag];
    if (!Object.keys(d.waage).length) delete d.waage;
  }
  return aenderung(true, _schreiben(d), 'Tag entfernt');
}

function spulenAendern(beschreibung, delta) {
  const d = _lesen(beschreibung);
  if (d === null) return aenderung(false, '', 'Eintrag nicht lesbar');
  const gewicht = _int(d.gewicht, 1000);
  const spulen = Math.max(0, _int(d.spulen, 1));
  const neu = Math.max(0, spulen + delta);
  if (neu === spulen) return aenderung(false, '', 'Keine Spule mehr da');
  d.spulen = neu;
  const reste = _reste(d.reste, gewicht);
  if (reste.length > neu) {
    reste.sort((x, y) => x - y);
    reste.shift();
    d.reste = reste;
  }
  if (_int(d.refills, 0) > neu) d.refills = neu;
  return aenderung(true, _schreiben(d), `${neu} Spule${neu === 1 ? '' : 'n'}`);
}

/** Nur in der Mac-App: einen angebrochenen Rest von Hand streichen. */
function restStreichen(beschreibung, gramm) {
  const d = _lesen(beschreibung);
  if (d === null) return aenderung(false, '', 'Eintrag nicht lesbar');
  const { gewicht } = _gewichtSpulen(d);
  const reste = _reste(d.reste, gewicht);
  const i = reste.indexOf(gramm);
  if (i < 0) return aenderung(false, '', 'Rest nicht gefunden');
  reste.splice(i, 1);
  d.reste = reste;
  if (_istDict(d.waage)) {
    for (const [k, v] of Object.entries(d.waage)) if (_int(v, -1) === gramm) { delete d.waage[k]; break; }
    if (!Object.keys(d.waage).length) delete d.waage;
  }
  return aenderung(true, _schreiben(d), `Rest ${gramm} g gestrichen`);
}

/** Nur in der Mac-App: Gewicht der leeren Spule für diese Sorte setzen (-1 = Standard). */
function leerSetzen(beschreibung, gramm) {
  const d = _lesen(beschreibung);
  if (d === null) return aenderung(false, '', 'Eintrag nicht lesbar');
  const g = _int(gramm, -1);
  if (g > 0) d.leer_g = g;
  else delete d.leer_g;
  return aenderung(true, _schreiben(d), g > 0 ? `Leerspule ${g} g` : 'Leerspule: Standard');
}

module.exports = {
  LEER_GRENZE_G, VOLL_TOLERANZ_G, MESS_TOLERANZ_G,
  hexZuRgb, sorteAusEintrag, lagerAusItems, lagerAusAntwort, findeNfc, findeUid,
  restSpeichern, tagZuordnen, tagEntfernen, spulenAendern, restStreichen, leerSetzen,
};
