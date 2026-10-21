'use strict';
/** Nachgebautes Home Assistant (WebSocket-API) für Tests – ohne Fremdpakete. */
const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

function rahmen(text) {
  const daten = Buffer.from(text);
  const n = daten.length;
  const kopf = n < 126 ? Buffer.from([0x81, n]) : n < 65536 ? Buffer.from([0x81, 126, n >> 8, n & 255]) : (() => {
    const b = Buffer.alloc(10); b[0] = 0x81; b[1] = 127; b.writeBigUInt64BE(BigInt(n), 2); return b;
  })();
  return Buffer.concat([kopf, daten]);
}

function starten({ token = 'geheim', registry = [], geraete = [], zustaende = [] } = {}) {
  const roh = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'testdaten', 'todo_filament.json'), 'utf8'));
  const items = roh.service_response['todo.filament'].items.map((x) => ({ ...x }));
  const aufrufe = [];
  const srv = http.createServer((_q, r) => r.end('nur WebSocket'));
  srv.on('upgrade', (req, sock) => {
    const key = req.headers['sec-websocket-key'];
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    const senden = (o) => sock.write(rahmen(JSON.stringify(o)));
    senden({ type: 'auth_required', ha_version: '2026.10.0' });
    let puffer = Buffer.alloc(0);
    sock.on('data', (b) => {
      puffer = Buffer.concat([puffer, b]);
      while (puffer.length >= 2) {
        const op = puffer[0] & 15;
        let n = puffer[1] & 127, o = 2;
        if (n === 126) { n = puffer.readUInt16BE(2); o = 4; } else if (n === 127) { n = Number(puffer.readBigUInt64BE(2)); o = 10; }
        const maske = puffer[1] & 128 ? puffer.subarray(o, o + 4) : null;
        if (maske) o += 4;
        if (puffer.length < o + n) return;
        const daten = Buffer.from(puffer.subarray(o, o + n));
        if (maske) for (let i = 0; i < n; i++) daten[i] ^= maske[i & 3];
        puffer = puffer.subarray(o + n);
        if (op === 8) { sock.end(); return; }
        if (op !== 1) continue;
        const m = JSON.parse(daten.toString());
        aufrufe.push(m);
        if (m.type === 'auth') senden(m.access_token === token ? { type: 'auth_ok' } : { type: 'auth_invalid', message: 'Invalid access token' });
        else if (m.type === 'get_states') senden({ id: m.id, type: 'result', success: true, result: zustaende });
        else if (m.type === 'config/entity_registry/list') senden({ id: m.id, type: 'result', success: true, result: registry });
        else if (m.type === 'config/device_registry/list') senden({ id: m.id, type: 'result', success: true, result: geraete });
        else if (m.type === 'subscribe_events') senden({ id: m.id, type: 'result', success: true, result: null });
        else if (m.type === 'call_service' && m.domain === 'todo' && m.service === 'get_items') {
          senden({ id: m.id, type: 'result', success: true, result: { context: {}, response: { [m.target.entity_id]: { items } } } });
        } else if (m.type === 'call_service' && m.domain === 'todo' && m.service === 'update_item') {
          const it = items.find((x) => x.uid === m.service_data.item);
          if (!it) senden({ id: m.id, type: 'result', success: false, error: { code: 'not_found', message: 'Unable to find to-do list item' } });
          else { it.description = m.service_data.description; senden({ id: m.id, type: 'result', success: true, result: { context: {} } }); }
        } else if (m.type === 'call_service') senden({ id: m.id, type: 'result', success: true, result: { context: {} } });
        else senden({ id: m.id, type: 'result', success: false, error: { code: 'unknown_command', message: 'Unknown command.' } });
      }
    });
    sock.on('error', () => {});
  });
  return new Promise((ja) => srv.listen(0, '127.0.0.1', () => ja({ url: `http://127.0.0.1:${srv.address().port}`, items, aufrufe, schliessen: () => srv.close() })));
}

module.exports = { starten };
