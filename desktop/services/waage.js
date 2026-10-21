'use strict';
/**
 * Live-Verbindung zur FilamentLager-Waage.
 *
 *  art "pi"     – über die Oberfläche der Pi- bzw. Pad-7-Waage (http://<adresse>:8080, Ereignisstrom).
 *                 Die Waage bleibt dabei ganz normal am Pad 7 bedienbar. Auf dem Pi muss in
 *                 /etc/filamentwaage/config.toml unter [server] adresse = "0.0.0.0" stehen.
 *  art "tcp"    – direkt zur Sensorplatine (TCP 4711, Protokoll aus docs/PROTOKOLL.md).
 *                 Achtung: Die Platine bedient nur eine Gegenstelle – ein Pad 7 wird dann getrennt.
 *  art "demo"   – ohne Hardware.
 *  art "aus"    – keine Waage.
 */
const { EventEmitter } = require('node:events');
const net = require('node:net');
const http = require('node:http');

const LEER = { gramm: 0, stabil: false, kalibriert: false, tagL: '', tagR: '', verbunden: false };

class Waage extends EventEmitter {
  constructor() {
    super();
    this.wert = { ...LEER };
    this.cfg = { art: 'aus' };
    this._stop = () => {};
  }

  konfigurieren(cfg) {
    this._stop();
    this._stop = () => {};
    this.cfg = { art: 'aus', adresse: 'filamentwaage.local', port: 0, ...cfg };
    this._setzen({ ...LEER });
    if (this.cfg.art === 'tcp') this._stop = this._tcp();
    else if (this.cfg.art === 'pi') this._stop = this._pi();
    else if (this.cfg.art === 'demo') this._stop = this._demo();
  }

  _setzen(teil) {
    this.wert = { ...this.wert, ...teil };
    this.emit('wert', this.wert);
  }

  _meldung(text, ok = true) { this.emit('meldung', { text, fehler: !ok }); }

  // ------------------------------------------------------------ Sensorplatine direkt
  _tcp() {
    let sock = null, frage = null, puffer = '', zuletzt = 0, aus = false, neu = null;
    const port = Number(this.cfg.port) || 4711;
    const senden = (o) => { if (sock && !sock.destroyed) sock.write(JSON.stringify(o) + '\n'); };
    this.befehl = (o) => senden(o);
    const verbinden = () => {
      if (aus) return;
      puffer = '';
      sock = net.createConnection({ host: this.cfg.adresse, port, timeout: 5000 });
      sock.setNoDelay(true);
      sock.on('connect', () => {
        sock.setTimeout(0);
        zuletzt = Date.now();
        frage = setInterval(() => {
          if (Date.now() - zuletzt > 3000) { sock.destroy(new Error('keine Antwort')); return; }
          senden({ c: 'p' });
        }, 100);
      });
      sock.on('data', (b) => {
        puffer += b.toString();
        let i;
        while ((i = puffer.indexOf('\n')) >= 0) {
          const zeile = puffer.slice(0, i).trim();
          puffer = puffer.slice(i + 1);
          if (!zeile.startsWith('{')) continue;
          let m;
          try { m = JSON.parse(zeile); } catch { continue; }
          zuletzt = Date.now();
          if (m.t === 's') {
            this._setzen({ gramm: Number(m.g) || 0, stabil: m.st === 1, kalibriert: m.cal === 1, tagL: m.L || '', tagR: m.R || '', verbunden: true });
          } else if (m.t === 'ok') {
            this._meldung(m.c === 'tara' ? 'Tara gesetzt' : 'Kalibriert');
          } else if (m.t === 'err') {
            this._meldung(m.msg || 'Fehler an der Waage', false);
          }
        }
      });
      sock.on('timeout', () => sock.destroy(new Error('Zeitüberschreitung')));
      sock.on('error', () => {});
      sock.on('close', () => {
        clearInterval(frage);
        this._setzen({ verbunden: false });
        if (!aus) neu = setTimeout(verbinden, 2000);
      });
    };
    verbinden();
    return () => { aus = true; clearTimeout(neu); clearInterval(frage); if (sock) sock.destroy(); this.befehl = null; };
  }

  // ------------------------------------------------------------ über die Pi-/Pad-7-Oberfläche
  _pi() {
    let aus = false, req = null, neu = null, wachhund = null, zuletzt = 0;
    const port = Number(this.cfg.port) || 8080;
    const basis = { host: this.cfg.adresse, port };
    this.befehl = (o) => {
      const pfad = o.c === 'tara' ? '/api/tara' : o.c === 'kal' ? '/api/kalibrieren' : null;
      if (!pfad) return;
      const body = JSON.stringify(o.c === 'kal' ? { g: o.g } : {});
      const r = http.request({ ...basis, path: pfad, method: 'POST', headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) } });
      r.on('error', (e) => this._meldung(`Waage nicht erreichbar (${e.code || e.message})`, false));
      r.end(body);
    };
    const verbinden = () => {
      if (aus) return;
      let puffer = '';
      zuletzt = Date.now();
      req = http.get({ ...basis, path: '/api/ereignisse', headers: { Accept: 'text/event-stream' } }, (res) => {
        if (res.statusCode !== 200) { res.resume(); req.destroy(); return; }
        res.setEncoding('utf8');
        res.on('data', (t) => {
          zuletzt = Date.now();
          puffer += t;
          let i;
          while ((i = puffer.indexOf('\n\n')) >= 0) {
            const block = puffer.slice(0, i);
            puffer = puffer.slice(i + 2);
            const art = /^event: (.*)$/m.exec(block)?.[1];
            const daten = /^data: (.*)$/m.exec(block)?.[1];
            if (!daten) continue;
            let d;
            try { d = JSON.parse(daten); } catch { continue; }
            if (art === 'zustand') {
              this._setzen({ gramm: Number(d.brutto_g) || 0, stabil: !!d.stabil, kalibriert: !!d.kalibriert, tagL: d.tag_l || '', tagR: d.tag_r || '', verbunden: !!d.sensor_ok, ueberPi: true });
            } else if (art === 'meldung' && /tara|kalibr/i.test(d.text || '')) {
              this._meldung(d.text, !d.fehler);
            }
          }
        });
        res.on('end', () => req.destroy());
      });
      req.on('error', () => {});
      req.on('close', () => {
        this._setzen({ verbunden: false, ueberPi: false });
        if (!aus) neu = setTimeout(verbinden, 2000);
      });
    };
    wachhund = setInterval(() => { if (req && Date.now() - zuletzt > 4000) req.destroy(); }, 1000);
    verbinden();
    return () => { aus = true; clearTimeout(neu); clearInterval(wachhund); if (req) req.destroy(); this.befehl = null; };
  }

  // ------------------------------------------------------------ ohne Hardware
  _demo() {
    // Szenen: leerer Teller → Blue Gray mit Tag (560 g Rest) → unbekannter Tag → leer
    const szenen = [
      { g: 0, L: '', R: '', s: 5 },
      { g: 812.4, L: '', R: '04C0FFEE112233', s: 12 },
      { g: 1130.0, L: '04DEADBEEF0001', R: '', s: 10 },
      { g: 0, L: '', R: '', s: 4 },
    ];
    let tara = 0, i = 0, t0 = Date.now();
    this.befehl = (o) => {
      if (o.c === 'tara') { tara = this.wert.gramm + tara; setTimeout(() => this._meldung('Tara gesetzt'), 800); }
      if (o.c === 'kal') setTimeout(() => this._meldung('Kalibriert'), 800);
    };
    const takt = setInterval(() => {
      const sz = szenen[i];
      const seit = (Date.now() - t0) / 1000;
      if (seit > sz.s) { i = (i + 1) % szenen.length; t0 = Date.now(); }
      const ziel = szenen[i].g;
      const einschwingen = Math.min(1, ((Date.now() - t0) / 1000) / 1.2);
      const rauschen = einschwingen < 1 ? (Math.random() - 0.5) * 40 * (1 - einschwingen) : (Math.random() - 0.5) * 0.4;
      this._setzen({
        gramm: Math.round((ziel * einschwingen + rauschen - (ziel ? tara : 0)) * 10) / 10,
        stabil: einschwingen >= 1, kalibriert: true, tagL: szenen[i].L, tagR: szenen[i].R, verbunden: true,
      });
    }, 150);
    return () => { clearInterval(takt); this.befehl = null; };
  }

  tara() { if (this.befehl) this.befehl({ c: 'tara' }); else this._meldung('Keine Waage verbunden', false); }
  kalibrieren(g) { if (this.befehl) this.befehl({ c: 'kal', g: Math.round(g) }); else this._meldung('Keine Waage verbunden', false); }
}

module.exports = { Waage };
