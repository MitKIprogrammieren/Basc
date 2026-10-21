'use strict';
// Waage: direkt zur Sensorplatine (nachgebaut) – Zeilenprotokoll aus docs/PROTOKOLL.md
const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { Waage } = require('../services/waage');

test('Sensorplatine per TCP: Messwert und Tara', async () => {
  const befehle = [];
  const srv = net.createServer((s) => {
    s.write('{"t":"hallo","fw":"1.0.0","nfc_l":true,"nfc_r":true,"cal":1}\n');
    let p = '';
    s.on('data', (b) => {
      p += b;
      let i;
      while ((i = p.indexOf('\n')) >= 0) {
        const m = JSON.parse(p.slice(0, i)); p = p.slice(i + 1);
        befehle.push(m.c);
        if (m.c === 'p') s.write('{"t":"s","g":812.4,"st":1,"cal":1,"L":"","R":"04C0FFEE112233"}\n');
        if (m.c === 'tara') setTimeout(() => s.write('{"t":"ok","c":"tara"}\n'), 50);
      }
    });
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const w = new Waage();
  const meldungen = [];
  w.on('meldung', (m) => meldungen.push(m.text));
  w.konfigurieren({ art: 'tcp', adresse: '127.0.0.1', port: srv.address().port });
  await new Promise((r) => setTimeout(r, 400));
  assert.equal(w.wert.verbunden, true);
  assert.equal(w.wert.gramm, 812.4);
  assert.equal(w.wert.tagR, '04C0FFEE112233');
  w.tara();
  await new Promise((r) => setTimeout(r, 300));
  assert.ok(befehle.includes('tara'));
  assert.ok(meldungen.includes('Tara gesetzt'));
  w.konfigurieren({ art: 'aus' });
  srv.close();
});
