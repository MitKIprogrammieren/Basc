'use strict';
// Oberfläche im normalen Browser ansehen – ohne Electron. Ersetzt window.fl durch eine Nachbildung mit Demo-Daten.
//   node test/vorschau/server.js  →  http://127.0.0.1:5173
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const WURZEL = path.join(__dirname, '..', '..');
const RENDERER = path.join(WURZEL, 'renderer');
const modul = (datei, name) => `(function(){const module={exports:{}};const require=(n)=>window.__m[n.replace(/^.*\\//,'')];\n${fs.readFileSync(path.join(WURZEL, datei), 'utf8')}\nwindow.__m['${name}']=module.exports;})();`;

http.createServer((req, res) => {
  let url = decodeURIComponent(req.url.split('?')[0]);
  if (url === '/') url = '/index.html';
  if (url === '/bruecke.js') {
    res.setHeader('Content-Type', 'text/javascript; charset=utf-8');
    const testdaten = fs.readFileSync(path.join(WURZEL, '..', 'testdaten', 'todo_filament.json'), 'utf8');
    return res.end(`window.__m={};${modul('services/modell.js', 'modell')}${modul('services/demo.js', 'demo')}window.__testdaten=${testdaten};\n${fs.readFileSync(path.join(__dirname, 'bruecke.js'), 'utf8')}`);
  }
  const datei = path.join(RENDERER, url);
  if (!datei.startsWith(RENDERER) || !fs.existsSync(datei)) { res.statusCode = 404; return res.end(); }
  let inhalt = fs.readFileSync(datei);
  if (url === '/index.html') {
    inhalt = Buffer.from(inhalt.toString().replace('<script src="lib/qrcode.min.js"></script>', '<script src="/bruecke.js"></script><script src="lib/qrcode.min.js"></script>'));
  }
  const typ = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.ttf': 'font/ttf' }[path.extname(datei)] || 'application/octet-stream';
  res.setHeader('Content-Type', typ + (typ.startsWith('text') ? '; charset=utf-8' : ''));
  res.end(inhalt);
}).listen(Number(process.env.PORT) || 5173, '127.0.0.1', () => console.log('Vorschau: http://127.0.0.1:' + (Number(process.env.PORT) || 5173)));
