'use strict';
const { contextBridge, ipcRenderer } = require('electron');

const aufruf = (kanal) => async (d) => {
  const r = await ipcRenderer.invoke(kanal, d);
  if (!r.ok) throw new Error(r.fehler);
  return r.wert;
};
const KANAELE = [
  'app:start', 'einst:speichern', 'ha:testen', 'lager:holen', 'lager:neu', 'lager:aendern',
  'drucker:holen', 'drucker:neu', 'drucker:filament', 'drucker:rfid', 'drucker:bild',
  'waage:tara', 'waage:kal', 'waage:holen',
  'etikett:png', 'etikett:drucken', 'extern:oeffnen', 'bt:waehlen',
];
const api = {};
for (const k of KANAELE) api[k.replace(':', '_')] = aufruf(k);
api.auf = (kanal, fn) => {
  if (!['lager', 'drucker', 'waage', 'meldung', 'ha-status', 'bt-geraete'].includes(kanal)) return;
  ipcRenderer.on(kanal, (_e, d) => fn(d));
};
contextBridge.exposeInMainWorld('fl', api);
