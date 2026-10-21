'use strict';
// Gleiche Fälle wie pi/tests/test_modell.py und firmware/display-5inch/test/host/test_model.cpp
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const m = require('../services/modell');

const BLUE_GRAY = JSON.stringify({
  v: 2, hersteller: 'Bambu Lab', material: 'PLA', farbe: 'Blue Gray', hex: '#5b6579',
  besonderheiten: ['Basic'], gewicht: 1000, spulen: 5, refills: 0, reste: [560], codes: [],
  codes_refill: [], durchmesser: 1.75, ausfuehrung: 'spule', duese_min: null, duese_max: null,
  bett_min: null, bett_max: null, trocknen_c: null, trocknen_h: null, fcode: '', bambu_id: 'GFA00',
});
const ASH_GRAY = JSON.stringify({
  v: 2, hersteller: 'Bambu Lab', material: 'PLA', farbe: 'Ash Gray', hex: '#9b9ea0',
  besonderheiten: ['Matt'], gewicht: 1000, spulen: 4, refills: 1, reste: [],
  codes: ['6975337030058'], duese_min: 190, duese_max: 230, trocknen_c: 55, trocknen_h: 8,
  fcode: '11102', bambu_id: 'GFA01',
});
const TESTDATEN = path.join(__dirname, '..', '..', 'testdaten', 'todo_filament.json');

test('Lager laden', () => {
  const sorten = m.lagerAusAntwort(JSON.parse(fs.readFileSync(TESTDATEN, 'utf8')), 'todo.filament');
  assert.equal(sorten.length, 25); // Einstellungen ist keine Sorte
  const bg = m.findeNfc(sorten, '04C0FFEE112233');
  assert.equal(bg.kurzname, 'PLA Blue Gray');
  assert.equal(bg.bestand_g, 4560);
});

test('Rest ohne Tag', () => {
  const a = m.restSpeichern(BLUE_GRAY, 540, '');
  const d = JSON.parse(a.beschreibung);
  assert.ok(a.geaendert);
  assert.deepEqual(d.reste, [540]);
  assert.equal(d.bambu_id, 'GFA00');
  assert.equal(d.duese_min, null);
  assert.ok(!('waage' in d));
});

test('Rest mit Tag', () => {
  let a = m.restSpeichern(BLUE_GRAY, 800, '04AA');
  let d = JSON.parse(a.beschreibung);
  assert.deepEqual(d.reste, [560, 800]);
  assert.deepEqual(d.waage, { '04AA': 800 });
  a = m.restSpeichern(a.beschreibung, 640, '04AA');
  d = JSON.parse(a.beschreibung);
  assert.deepEqual(d.reste, [560, 640]);
  a = m.restSpeichern(a.beschreibung, 3, '04AA');
  d = JSON.parse(a.beschreibung);
  assert.equal(d.spulen, 4);
  assert.deepEqual(d.reste, [560]);
  assert.ok(!('waage' in d));
});

test('voll und leer', () => {
  assert.equal(m.restSpeichern(ASH_GRAY, 995, '').geaendert, false);
  let d = JSON.parse(m.restSpeichern('{"gewicht":1000,"spulen":1,"refills":1,"reste":[]}', 0, '').beschreibung);
  assert.deepEqual([d.spulen, d.refills], [0, 0]);
  d = JSON.parse(m.restSpeichern('{"gewicht":1000,"spulen":0,"reste":[]}', 400, '').beschreibung);
  assert.deepEqual([d.spulen, d.reste], [1, [400]]);
});

test('Tags', () => {
  const a = m.tagZuordnen(ASH_GRAY, '04BB');
  assert.ok(a.geaendert);
  assert.deepEqual(m.sorteAusEintrag('u', 'x', a.beschreibung).nfc, ['04BB']);
  assert.equal(m.tagZuordnen(a.beschreibung, '04BB').geaendert, false);
  const b = m.tagEntfernen(a.beschreibung, '04BB');
  assert.ok(!('nfc' in JSON.parse(b.beschreibung)));
});

test('Spulen', () => {
  assert.equal(JSON.parse(m.spulenAendern(BLUE_GRAY, -1).beschreibung).spulen, 4);
  const d = JSON.parse(m.spulenAendern('{"spulen":1,"reste":[300]}', -1).beschreibung);
  assert.deepEqual([d.spulen, d.reste], [0, []]);
  assert.equal(m.spulenAendern('{"spulen":0}', -1).geaendert, false);
});

test('Felder bleiben in Reihenfolge, unbekannte bleiben erhalten', () => {
  const ein = '{"v":2,"x_unbekannt":{"a":1},"gewicht":1000,"spulen":2,"reste":[300],"preis":19.99}';
  const aus = m.restSpeichern(ein, 250, '').beschreibung;
  assert.equal(aus, '{"v":2,"x_unbekannt":{"a":1},"gewicht":1000,"spulen":2,"reste":[250],"preis":19.99}');
});

test('Mac-App: Rest streichen und Leerspule', () => {
  let d = JSON.parse(m.restStreichen('{"spulen":2,"reste":[300,500],"waage":{"04AA":500}}', 500).beschreibung);
  assert.deepEqual(d.reste, [300]);
  assert.ok(!('waage' in d));
  d = JSON.parse(m.leerSetzen('{"spulen":1}', 180).beschreibung);
  assert.equal(d.leer_g, 180);
  d = JSON.parse(m.leerSetzen(JSON.stringify(d), -1).beschreibung);
  assert.ok(!('leer_g' in d));
});
