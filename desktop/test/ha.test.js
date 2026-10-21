'use strict';
// Home-Assistant-Anbindung gegen ein nachgebautes HA: Lager lesen, Rest schreiben, Dienstaufruf
const test = require('node:test');
const assert = require('node:assert/strict');
const { Ha } = require('../services/ha');
const modell = require('../services/modell');
const mock = require('./mock-ha');

const bereit = (ha) => new Promise((ja, nein) => {
  ha.once('bereit', ja);
  ha.on('status', (s) => { if (!s.verbunden && s.grund) nein(new Error(s.grund)); });
});

test('Lager lesen und Rest zurückschreiben', async () => {
  const m = await mock.starten();
  const ha = new Ha();
  const p = bereit(ha);
  ha.konfigurieren({ url: m.url, token: 'geheim' });
  await p;
  const sorten = modell.lagerAusItems(await ha.todoEintraege('todo.filament'));
  assert.equal(sorten.length, 25);
  const bg = modell.findeNfc(sorten, '04C0FFEE112233');
  const a = modell.restSpeichern(bg.beschreibung, 540, '04C0FFEE112233');
  await ha.todoBeschreibung('todo.filament', bg.uid, a.beschreibung);
  const neu = modell.findeUid(modell.lagerAusItems(await ha.todoEintraege('todo.filament')), bg.uid);
  assert.deepEqual(neu.reste, [540]);
  assert.deepEqual(neu.waage, { '04C0FFEE112233': 540 });
  await ha.dienst('bambu_lab', 'set_filament', { entity_id: 'sensor.x', tray_info_idx: 'GFA00' });
  assert.ok(m.aufrufe.some((x) => x.domain === 'bambu_lab' && x.service === 'set_filament'));
  await assert.rejects(ha.todoBeschreibung('todo.filament', 'gibtsnicht', '{}'), /Unable to find/);
  ha.beenden(); m.schliessen();
});

test('falsches Token wird gemeldet', async () => {
  const m = await mock.starten();
  const ha = new Ha();
  const p = bereit(ha);
  ha.konfigurieren({ url: m.url, token: 'falsch' });
  await assert.rejects(p, /Token abgelehnt/);
  ha.beenden(); m.schliessen();
});
