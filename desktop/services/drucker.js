'use strict';
/**
 * Bambu-Lab-Drucker und AMS aus der Home-Assistant-Integration ha-bambulab.
 *
 * Erkannt wird über die unique_id der Entitäten (sprachunabhängig, die entity_id ist bei
 * dir z. B. deutsch): Drucker "SERIAL_key", AMS "TYP_SERIAL_AMS_AMSSERIAL_key",
 * externe Spule "TYP_SERIAL_ExternalSpoolX_external_spool".
 */

const DRUCKER_KEYS = new Set([
  'print_status', 'print_progress', 'remaining_time', 'end_time', 'nozzle_temp', 'left_nozzle_temp',
  'right_nozzle_temp', 'target_nozzle_temp', 'bed_temp', 'target_bed_temp', 'chamber_temp', 'current_layer',
  'total_layers', 'subtask_name', 'active_tray', 'online', 'cover_image', 'printer_name', 'stage', 'print_weight',
]);

// Allgemeine Bambu-Filament-IDs, wenn die Sorte keine eigene bambu_id hat
const GENERISCH = [
  [/silk/i, 'GFL96'], [/pla.*cf|pla-cf/i, 'GFL98'], [/pla.*(hs|high speed)/i, 'GFL95'], [/^pla/i, 'GFL99'],
  [/petg.*hf/i, 'GFG96'], [/petg.*cf/i, 'GFG98'], [/pctg/i, 'GFG97'], [/petg|pet/i, 'GFG99'],
  [/asa/i, 'GFB98'], [/abs/i, 'GFB99'], [/tpu/i, 'GFU99'], [/pa.*cf|pa-cf|paht/i, 'GFN98'], [/^pa|nylon/i, 'GFN99'],
  [/^pc/i, 'GFC99'], [/pva/i, 'GFS99'], [/hips/i, 'GFS98'], [/^pp\b/i, 'GFP97'],
];
const TEMP_STANDARD = { PLA: [190, 230], PETG: [230, 260], ABS: [240, 270], ASA: [240, 270], TPU: [200, 240], PA: [260, 290], PC: [260, 290] };

function schluessel(uniqueId) {
  let m = /^([^_]+)_([^_]+)_AMS_([^_]+)_(.+)$/.exec(uniqueId);
  if (m) return { art: 'ams', typ: m[1], serial: m[2], ams: m[3], key: m[4] };
  m = /^([^_]+)_([^_]+)_ExternalSpool([^_]*)_(.+)$/.exec(uniqueId);
  if (m) return { art: 'extern', typ: m[1], serial: m[2], ams: 'ext' + m[3], key: m[4] };
  if (/_HotendRack_/.test(uniqueId)) return null;
  m = /^([^_]+)_(.+)$/.exec(uniqueId);
  if (m) return { art: 'drucker', serial: m[1], key: m[2] };
  return null;
}

/** Registry-Einträge der Integration bambu_lab → Zuordnung entity_id → Rolle */
function karteAusRegistry(entitaeten, geraete = []) {
  const geraetName = new Map(geraete.map((g) => [g.id, g]));
  const drucker = new Map();
  const rollen = new Map();
  const holen = (serial) => {
    if (!drucker.has(serial)) drucker.set(serial, { serial, typ: '', geraet: null, entities: {}, ams: new Map() });
    return drucker.get(serial);
  };
  for (const e of entitaeten) {
    if (e.platform !== 'bambu_lab' || e.disabled_by || !e.unique_id) continue;
    const k = schluessel(String(e.unique_id));
    if (!k || (k.art === 'drucker' && !DRUCKER_KEYS.has(k.key))) continue;
    const d = holen(k.serial);
    if (k.typ) d.typ = k.typ;
    if (k.art === 'drucker') {
      d.entities[k.key] = e.entity_id;
      if (e.device_id && !d.geraet) d.geraet = geraetName.get(e.device_id) || null;
      rollen.set(e.entity_id, k.serial);
    } else {
      if (!d.ams.has(k.ams)) {
        const nr = /_ams_(\d+)_/i.exec(e.entity_id);
        d.ams.set(k.ams, { id: k.ams, extern: k.art === 'extern', index: nr ? Number(nr[1]) : null, entities: {}, slots: {} });
      }
      const a = d.ams.get(k.ams);
      const slot = /^tray_(\d+)$/.exec(k.key);
      if (slot) a.slots[Number(slot[1])] = e.entity_id;
      else if (k.key === 'external_spool') a.slots[1] = e.entity_id;
      else if (['humidity', 'humidity_index', 'temperature', 'remaining_drying_time', 'drying'].includes(k.key)) a.entities[k.key] = e.entity_id;
      else continue;
      rollen.set(e.entity_id, k.serial);
    }
  }
  return { drucker, rollen };
}

const zahl = (st) => {
  if (!st || st.state === 'unavailable' || st.state === 'unknown' || st.state === '') return null;
  const n = Number(st.state);
  return Number.isFinite(n) ? n : null;
};
const text = (st) => (!st || st.state === 'unavailable' || st.state === 'unknown' ? '' : String(st.state));

function amsName(a, alle, externe) {
  if (a.extern) {
    if (externe.length < 2) return { name: 'Externe Spule', kurz: 'Ext' };
    const n = externe.indexOf(a) + 1;
    return { name: `Externe Spule ${n}`, kurz: `Ext${n}` };
  }
  if (a.index != null && a.index >= 128) {
    const n = a.index - 127;
    return { name: `AMS HT ${n}`, kurz: `HT${n}` };
  }
  const i = a.index != null ? a.index : alle.indexOf(a) + 1;
  return { name: `AMS ${i}`, kurz: String.fromCharCode(64 + Math.max(1, i)) };
}

function farbe(attr) {
  const c = String(attr?.color || '').replace('#', '');
  return c.length >= 6 ? '#' + c.slice(0, 6).toLowerCase() : '#888888';
}

/** Momentaufnahme aller Drucker aus der Karte und den Zuständen */
function druckerListe(karte, zustaende, namen = {}) {
  const st = (id) => (id ? zustaende.get(id) : undefined);
  const liste = [];
  for (const d of karte.drucker.values()) {
    const e = d.entities;
    const modell = d.geraet?.model || d.typ || 'Bambu Lab';
    const name = namen[d.serial] || d.geraet?.name_by_user || text(st(e.printer_name)) ||
      `${modell} …${d.serial.slice(-4)}`;
    const onlineSt = st(e.online);
    const status = text(st(e.print_status)) || 'offline';
    const online = onlineSt ? onlineSt.state === 'on' : status !== 'offline';
    const titel = st(e.cover_image);
    const amsAlle = [...d.ams.values()].sort((x, y) => Number(x.extern) - Number(y.extern) || (x.index ?? 99) - (y.index ?? 99));
    const amsNormal = amsAlle.filter((a) => !a.extern);
    const amsExtern = amsAlle.filter((a) => a.extern).sort((x, y) => x.id.localeCompare(y.id));
    const ams = amsAlle.map((a) => {
      const n = amsName(a, amsNormal, amsExtern);
      const slots = Object.keys(a.slots).map(Number).sort((x, y) => x - y).map((nr) => {
        const s = st(a.slots[nr]);
        const at = s?.attributes || {};
        const leer = !s || s.state === 'unavailable' || at.empty === true || (!at.type && !at.name);
        return {
          slot: nr, entity: a.slots[nr], leer, aktiv: at.active === true,
          name: at.name || (leer ? '' : text(s)), typ: at.type || '', hex: farbe(at),
          filament_id: at.filament_id || '', tag_uid: at.tag_uid && !/^0+$/.test(at.tag_uid) ? String(at.tag_uid) : '',
          tray_uuid: at.tray_uuid && !/^0+$/.test(at.tray_uuid) ? String(at.tray_uuid) : '',
          rest: typeof at.remain === 'number' && at.remain >= 0 && at.remain_enabled !== false ? at.remain : -1,
          duese_min: at.nozzle_temp_min ?? null, duese_max: at.nozzle_temp_max ?? null,
        };
      });
      return {
        id: a.id, extern: a.extern, name: n.name, kurz: n.kurz,
        feuchte: zahl(st(a.entities.humidity)), feuchte_index: zahl(st(a.entities.humidity_index)),
        temperatur: zahl(st(a.entities.temperature)), slots,
      };
    });
    const duese = zahl(st(e.nozzle_temp)) ?? zahl(st(e.left_nozzle_temp));
    liste.push({
      id: d.serial, name, modell, online, status,
      fortschritt: zahl(st(e.print_progress)), rest_min: zahl(st(e.remaining_time)),
      endzeit: text(st(e.end_time)), auftrag: text(st(e.subtask_name)),
      schicht: zahl(st(e.current_layer)), schichten: zahl(st(e.total_layers)),
      duese, duese_rechts: zahl(st(e.right_nozzle_temp)), duese_ziel: zahl(st(e.target_nozzle_temp)),
      bett: zahl(st(e.bed_temp)), bett_ziel: zahl(st(e.target_bed_temp)), raum: zahl(st(e.chamber_temp)),
      titelbild: titel?.attributes?.entity_picture || '', titelbild_stand: titel?.last_updated || '',
      ams,
    });
  }
  return liste.sort((a, b) => a.name.localeCompare(b.name, 'de'));
}

/** Daten für bambu_lab.set_filament aus einer Sorte */
function filamentFuerSlot(sorte) {
  const material = sorte.material || 'PLA';
  let idx = /^GF[A-Z0-9]{3}$/i.test(sorte.bambu_id) ? sorte.bambu_id.toUpperCase() : '';
  if (!idx) {
    const t = `${material} ${sorte.besonderheiten.join(' ')}`;
    idx = (GENERISCH.find(([re]) => re.test(t)) || [null, 'GFL99'])[1];
  }
  const basis = Object.keys(TEMP_STANDARD).find((k) => material.toUpperCase().startsWith(k)) || 'PLA';
  const [tmin, tmax] = TEMP_STANDARD[basis];
  const klemm = (v) => Math.max(160, Math.min(300, v));
  return {
    tray_info_idx: idx,
    tray_color: sorte.hex.replace('#', '').toUpperCase() + 'FF',
    tray_type: material.split(/[\s+]/)[0].toUpperCase(),
    nozzle_temp_min: klemm(sorte.duese_min > 0 ? sorte.duese_min : tmin),
    nozzle_temp_max: klemm(sorte.duese_max > 0 ? sorte.duese_max : tmax),
  };
}

/** Passende Sorte zu einem AMS-Slot: zuerst NFC-UID, dann Bambu-ID + Farbe */
function sorteFuerSlot(sorten, slot) {
  if (slot.leer) return null;
  if (slot.tag_uid) {
    const t = slot.tag_uid.toUpperCase();
    const s = sorten.find((x) => x.nfc.some((n) => n.toUpperCase().startsWith(t.slice(0, 8)) || t.startsWith(n.toUpperCase().slice(0, 8))));
    if (s) return s;
  }
  const kand = sorten.filter((x) => x.bambu_id && x.bambu_id === slot.filament_id);
  if (!kand.length) return null;
  const abst = (h1, h2) => {
    const a = parseInt(h1.slice(1), 16), b = parseInt(h2.slice(1), 16);
    return Math.abs((a >> 16) - (b >> 16)) + Math.abs(((a >> 8) & 255) - ((b >> 8) & 255)) + Math.abs((a & 255) - (b & 255));
  };
  kand.sort((x, y) => abst(x.hex, slot.hex) - abst(y.hex, slot.hex));
  return abst(kand[0].hex, slot.hex) < 60 ? kand[0] : null;
}

module.exports = { schluessel, karteAusRegistry, druckerListe, filamentFuerSlot, sorteFuerSlot };
