'use strict';
/**
 * Home Assistant über die WebSocket-API.
 * Eine Verbindung für alles: Lager (todo.filament), Zustände der Bambu-Lab-Integration
 * und Live-Änderungen (state_changed). Verbindet sich bei Abbruch selbst neu.
 */
const { EventEmitter } = require('node:events');

// "ws" kann eigene Zertifikate erlauben; ohne das Paket reicht der eingebaute WebSocket von Node/Electron.
let WsPaket = null;
try { WsPaket = require('ws'); } catch { WsPaket = null; }

function oeffnen(url, zertifikatPruefen) {
  if (WsPaket) return new WsPaket(url, { rejectUnauthorized: zertifikatPruefen, handshakeTimeout: 8000 });
  const roh = new globalThis.WebSocket(url);
  const h = new EventEmitter();
  roh.addEventListener('message', (e) => h.emit('message', typeof e.data === 'string' ? e.data : Buffer.from(e.data)));
  roh.addEventListener('error', () => h.emit('error', new Error('Verbindungsfehler')));
  roh.addEventListener('close', () => h.emit('close'));
  h.send = (d) => roh.send(d);
  h.close = () => roh.close();
  h.terminate = () => { try { roh.close(); } catch { /* egal */ } };
  return h;
}

class HaFehler extends Error {}

class Ha extends EventEmitter {
  constructor() {
    super();
    this.ws = null;
    this.id = 1;
    this.offen = new Map();
    this.bereit = false;
    this.cfg = null;
    this._neuTimer = null;
    this._warte = 2000;
    this._beendet = false;
  }

  konfigurieren({ url, token, zertifikatPruefen = true }) {
    this.cfg = { url: (url || '').replace(/\/+$/, ''), token: token || '', zertifikatPruefen };
    this._schliessen();
    this._beendet = false;
    if (this.cfg.url && this.cfg.token) this._verbinden();
  }

  beenden() {
    this._beendet = true;
    this._schliessen();
  }

  _schliessen() {
    clearTimeout(this._neuTimer);
    if (this.ws) {
      this.ws.removeAllListeners();
      try { this.ws.terminate(); } catch { /* egal */ }
    }
    this.ws = null;
    this._setzeBereit(false, '');
    for (const { nein } of this.offen.values()) nein(new HaFehler('Verbindung zu Home Assistant getrennt'));
    this.offen.clear();
  }

  _setzeBereit(ok, grund) {
    const vorher = this.bereit;
    this.bereit = ok;
    if (ok !== vorher || grund) this.emit('status', { verbunden: ok, grund: grund || '' });
  }

  _verbinden() {
    const wsUrl = this.cfg.url.replace(/^http/, 'ws') + '/api/websocket';
    let ws;
    try {
      ws = oeffnen(wsUrl, this.cfg.zertifikatPruefen);
    } catch (e) {
      this._setzeBereit(false, `Adresse ungültig (${e.message})`);
      return;
    }
    this.ws = ws;
    ws.on('message', (roh) => this._nachricht(roh));
    ws.on('error', (e) => this._setzeBereit(false, `nicht erreichbar (${e.code || e.message})`));
    ws.on('close', () => {
      if (this.ws !== ws) return;
      this.ws = null;
      this._setzeBereit(false, this._grund || 'Verbindung getrennt');
      for (const { nein } of this.offen.values()) nein(new HaFehler('Verbindung zu Home Assistant getrennt'));
      this.offen.clear();
      if (!this._beendet && this._grund !== 'Token abgelehnt') {
        this._neuTimer = setTimeout(() => this._verbinden(), this._warte);
        this._warte = Math.min(this._warte * 2, 30000);
      }
    });
  }

  _nachricht(roh) {
    let m;
    try { m = JSON.parse(roh.toString()); } catch { return; }
    if (m.type === 'auth_required') {
      this.ws.send(JSON.stringify({ type: 'auth', access_token: this.cfg.token }));
    } else if (m.type === 'auth_ok') {
      this._grund = '';
      this._warte = 2000;
      this._setzeBereit(true, '');
      this.emit('bereit');
    } else if (m.type === 'auth_invalid') {
      this._grund = 'Token abgelehnt';
      this.ws.close();
    } else if (m.type === 'event') {
      this.emit('ereignis', m.event);
    } else if (m.type === 'result') {
      const w = this.offen.get(m.id);
      if (!w) return;
      this.offen.delete(m.id);
      if (m.success) w.ja(m.result);
      else w.nein(new HaFehler(m.error?.message || 'Home Assistant meldet einen Fehler'));
    }
  }

  anfrage(daten, timeoutMs = 15000) {
    if (!this.bereit || !this.ws) return Promise.reject(new HaFehler('Home Assistant nicht verbunden'));
    const id = this.id++;
    return new Promise((ja, nein) => {
      const t = setTimeout(() => {
        this.offen.delete(id);
        nein(new HaFehler('Home Assistant antwortet nicht'));
      }, timeoutMs);
      this.offen.set(id, {
        ja: (r) => { clearTimeout(t); ja(r); },
        nein: (e) => { clearTimeout(t); nein(e); },
      });
      this.ws.send(JSON.stringify({ id, ...daten }));
    });
  }

  dienst(domain, service, serviceData = {}, target = undefined, mitAntwort = false) {
    const d = { type: 'call_service', domain, service, service_data: serviceData };
    if (target) d.target = target;
    if (mitAntwort) d.return_response = true;
    return this.anfrage(d);
  }

  async todoEintraege(entity) {
    const r = await this.dienst('todo', 'get_items', {}, { entity_id: entity }, true);
    const items = r?.response?.[entity]?.items;
    if (!Array.isArray(items)) throw new HaFehler(`Antwort nicht lesbar – gibt es ${entity}?`);
    return items;
  }

  todoBeschreibung(entity, uid, beschreibung) {
    return this.dienst('todo', 'update_item', { item: uid, description: beschreibung }, { entity_id: entity });
  }

  zustaende() { return this.anfrage({ type: 'get_states' }, 30000); }
  entitaeten() { return this.anfrage({ type: 'config/entity_registry/list' }, 30000); }
  geraete() { return this.anfrage({ type: 'config/device_registry/list' }, 30000); }
  abonnieren(ereignis) { return this.anfrage({ type: 'subscribe_events', event_type: ereignis }); }

  /** Bild aus HA (z. B. Titelbild des Drucks) als data:-URL */
  async bild(pfad) {
    if (!this.cfg?.url || !pfad) return null;
    const url = /^https?:/.test(pfad) ? pfad : this.cfg.url + pfad;
    const r = await fetch(url, { headers: { Authorization: `Bearer ${this.cfg.token}` } });
    if (!r.ok) return null;
    const typ = r.headers.get('content-type') || 'image/png';
    return `data:${typ};base64,${Buffer.from(await r.arrayBuffer()).toString('base64')}`;
  }
}

module.exports = { Ha, HaFehler };
