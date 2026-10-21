'use strict';
// unique_ids so, wie sie in Home Assistant (ha-bambulab) wirklich vorkommen
const test = require('node:test');
const assert = require('node:assert/strict');
const dr = require('../services/drucker');

const E = (entity_id, unique_id, device_id = 'dev') => ({ entity_id, unique_id, platform: 'bambu_lab', device_id, disabled_by: null });
const REG = [
  E('sensor.x1c_00m09a320701003_druckstatus', '00M09A320701003_print_status', 'p1'),
  E('sensor.x1c_00m09a320701003_druckfortschritt', '00M09A320701003_print_progress', 'p1'),
  E('binary_sensor.x1c_00m09a320701003_online', '00M09A320701003_online', 'p1'),
  E('image.x1c_00m09a320701003_titelbild', '00M09A320701003_cover_image', 'p1'),
  E('sensor.x1c_00m09a320701003_ams_1_slot_1', 'X1C_00M09A320701003_AMS_00600A2B0801580_tray_1', 'a1'),
  E('sensor.x1c_00m09a320701003_ams_1_slot_2', 'X1C_00M09A320701003_AMS_00600A2B0801580_tray_2', 'a1'),
  E('sensor.x1c_00m09a320701003_ams_1_luftfeuchtigkeit', 'X1C_00M09A320701003_AMS_00600A2B0801580_humidity', 'a1'),
  E('sensor.x1c_00m09a320701003_externalspool_externe_spule', 'X1C_00M09A320701003_ExternalSpool_external_spool', 'x1'),
  E('sensor.h2c_31b8bk631200165_linke_dusentemperatur', '31B8BK631200165_left_nozzle_temp', 'p2'),
  E('sensor.h2c_31b8bk631200165_ams_128_slot_1', 'H2C_31B8BK631200165_AMS_19F06A560200215_tray_1', 'a2'),
  E('sensor.h2c_31b8bk631200165_ams_1_slot_1', 'H2C_31B8BK631200165_AMS_0A1_tray_1', 'a3'),
  E('sensor.h2c_31b8bk631200165_externalspool_externe_spule', 'H2C_31B8BK631200165_ExternalSpool_external_spool', 'x2'),
  E('sensor.h2c_31b8bk631200165_externalspool2_externe_spule', 'H2C_31B8BK631200165_ExternalSpool2_external_spool', 'x3'),
  E('sensor.h2c_31b8bk631200165_hotendrack_hotend_1', 'H2C_31B8BK631200165_HotendRack_hotend_16', 'r1'),
  { entity_id: 'light.anderes', unique_id: 'x_y', platform: 'hue' },
];
const GER = [{ id: 'p1', model: 'X1C', name: 'X1C_00M09A320701003', name_by_user: null }, { id: 'p2', model: 'H2C', name: 'H2C_31B8BK631200165', name_by_user: 'H2C Werkstatt' }];

test('Drucker, AMS und externe Spulen aus der Registry', () => {
  const karte = dr.karteAusRegistry(REG, GER);
  assert.deepEqual([...karte.drucker.keys()].sort(), ['00M09A320701003', '31B8BK631200165']);
  const zust = new Map([
    ['sensor.x1c_00m09a320701003_druckstatus', { state: 'running' }],
    ['sensor.x1c_00m09a320701003_druckfortschritt', { state: '42' }],
    ['binary_sensor.x1c_00m09a320701003_online', { state: 'on' }],
    ['sensor.x1c_00m09a320701003_ams_1_luftfeuchtigkeit', { state: '23' }],
    ['sensor.x1c_00m09a320701003_ams_1_slot_1', { state: 'Bambu PLA Basic', attributes: { name: 'Bambu PLA Basic', type: 'PLA', color: '#5B6579FF', filament_id: 'GFA00', tag_uid: '04C0FFEE', remain: 61, active: true, empty: false } }],
    ['sensor.x1c_00m09a320701003_ams_1_slot_2', { state: 'Leer', attributes: { empty: true, color: '#00000000', tag_uid: '0000000000000000', remain: -1 } }],
  ]);
  const [h2c, x1c] = dr.druckerListe(karte, zust, {});
  assert.equal(h2c.name, 'H2C Werkstatt');
  assert.deepEqual(h2c.ams.map((a) => a.name), ['AMS 1', 'AMS HT 1', 'Externe Spule 1', 'Externe Spule 2']);
  assert.equal(h2c.status, 'offline');
  assert.equal(x1c.name, 'X1C …1003');
  assert.equal(x1c.status, 'running');
  assert.equal(x1c.fortschritt, 42);
  const a = x1c.ams[0];
  assert.equal(a.feuchte, 23);
  assert.equal(a.slots[0].hex, '#5b6579');
  assert.equal(a.slots[0].aktiv, true);
  assert.equal(a.slots[0].rest, 61);
  assert.equal(a.slots[1].leer, true);
  assert.equal(a.slots[1].tag_uid, '');
  assert.equal(x1c.ams[1].name, 'Externe Spule');
});

test('Filament für den Slot', () => {
  const s = { material: 'PETG', besonderheiten: ['HF'], bambu_id: '', hex: '#ff8800', duese_min: -1, duese_max: -1 };
  assert.deepEqual(dr.filamentFuerSlot(s), { tray_info_idx: 'GFG96', tray_color: 'FF8800FF', tray_type: 'PETG', nozzle_temp_min: 230, nozzle_temp_max: 260 });
  const b = { material: 'PLA', besonderheiten: ['Matt'], bambu_id: 'GFA01', hex: '#9b9ea0', duese_min: 190, duese_max: 230 };
  assert.equal(dr.filamentFuerSlot(b).tray_info_idx, 'GFA01');
  assert.equal(dr.filamentFuerSlot({ ...b, bambu_id: '', besonderheiten: ['Silk'] }).tray_info_idx, 'GFL96');
});
