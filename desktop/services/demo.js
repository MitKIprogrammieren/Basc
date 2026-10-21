'use strict';
/** Beispieldrucker für den Demo-Modus (ohne Home Assistant). */

function slot(nr, sorte, extra = {}) {
  if (!sorte) return { slot: nr, entity: `demo.slot_${nr}_${Math.random().toString(36).slice(2, 7)}`, leer: true, aktiv: false, name: '', typ: '', hex: '#888888', filament_id: '', tag_uid: '', tray_uuid: '', rest: -1, duese_min: null, duese_max: null };
  return {
    slot: nr, entity: `demo.slot_${sorte.uid}_${nr}_${Math.random().toString(36).slice(2, 7)}`, leer: false, aktiv: false,
    name: `${sorte.hersteller === 'Bambu Lab' ? 'Bambu ' : ''}${sorte.material} ${sorte.besonderheiten[0] || ''}`.trim(),
    typ: sorte.material, hex: sorte.hex, filament_id: sorte.bambu_id, tag_uid: (sorte.nfc[0] || '').slice(0, 8) + '00000000',
    tray_uuid: '', rest: 40 + Math.round(Math.random() * 60), duese_min: sorte.duese_min, duese_max: sorte.duese_max, ...extra,
  };
}

function demoDrucker(sorten) {
  const s = (i) => sorten[i % sorten.length];
  const ams = (id, name, kurz, belegt, feuchte = 22) => ({ id, extern: false, name, kurz, feuchte, feuchte_index: 1, temperatur: 26, slots: belegt.map((x, i) => slot(i + 1, x)) });
  const ext = (x) => ({ id: 'ext', extern: true, name: 'Externe Spule', kurz: 'Ext', feuchte: null, feuchte_index: null, temperatur: null, slots: [slot(1, x)] });
  const basis = { online: true, status: 'idle', fortschritt: null, rest_min: null, endzeit: '', auftrag: '', schicht: null, schichten: null, duese: 26, duese_rechts: null, duese_ziel: 0, bett: 24, bett_ziel: 0, raum: null, titelbild: '', titelbild_stand: '' };
  const jetzt = Date.now();
  const liste = [
    { ...basis, id: '31B8BK631200165', name: 'H2C', modell: 'H2C', status: 'running', fortschritt: 63, rest_min: 84,
      endzeit: new Date(jetzt + 84 * 60000).toISOString(), auftrag: 'Spulenhalter_v3', schicht: 212, schichten: 340,
      duese: 220, duese_rechts: 215, duese_ziel: 220, bett: 55, bett_ziel: 55, raum: 34,
      ams: [ams('ams1', 'AMS 1', 'A', [s(1), s(0), s(4), s(7)]), ams('ht1', 'AMS HT 1', 'HT1', [s(9)], 18), ext(null)] },
    { ...basis, id: '0948BB540900123', name: 'H2S', modell: 'H2S', ams: [ams('ams1', 'AMS 1', 'A', [s(2), s(5), null, s(11)]), ext(null)] },
    { ...basis, id: '00M00A2B0805993', name: 'X1C links', modell: 'X1C', status: 'finish', fortschritt: 100, rest_min: 0, auftrag: 'Gehäuse_Deckel',
      ams: [ams('ams1', 'AMS 1', 'A', [s(3), s(6), s(8), s(1)], 31), ams('ams2', 'AMS 2', 'B', [s(12), s(13), null, s(14)], 29), ext(null)] },
    { ...basis, id: '00M09A320701003', name: 'X1C rechts', modell: 'X1C', status: 'running', fortschritt: 18, rest_min: 211,
      endzeit: new Date(jetzt + 211 * 60000).toISOString(), auftrag: 'Kabelkanal_4x', schicht: 31, schichten: 180,
      duese: 250, duese_ziel: 250, bett: 70, bett_ziel: 70, raum: 41,
      ams: [ams('ams1', 'AMS 1', 'A', [s(15), s(16), s(17), s(18)]), ext(null)] },
    { ...basis, id: '039E1A1B2300456', name: 'A1', modell: 'A1', ams: [ams('lite', 'AMS 1', 'A', [s(19), s(20), s(21), s(0)]), ext(s(22))] },
    { ...basis, id: '0309DA422600899', name: 'A1 mini 1', modell: 'A1 mini', online: false, status: 'offline', duese: null, bett: null, ams: [ext(null)] },
    { ...basis, id: '0309DA441000322', name: 'A1 mini 2', modell: 'A1 mini', ams: [ext(s(23))] },
  ];
  liste[0].ams[0].slots[1].aktiv = true;
  liste[3].ams[0].slots[2].aktiv = true;
  return liste;
}

module.exports = { demoDrucker };
