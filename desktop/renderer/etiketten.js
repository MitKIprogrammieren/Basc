'use strict';
/* Etiketten: Spulen-Etiketten gestalten und drucken (Phomemo M20Pro per Bluetooth, Druckdialog oder PNG) */

const E = { auswahl: new Set(), q: '', nurAuswahl: false };
const GROESSEN = [[50, 30], [40, 30], [50, 50], [40, 20], [30, 20], [60, 40]];
const FELDER = [['hersteller', 'Hersteller'], ['temperatur', 'Düsentemperatur'], ['trocknen', 'Trocknen'], ['hex', 'Farbcode'], ['nfc', 'NFC-Tag']];

function etikettCfg() { return S.einst.etikett; }

/** Ein Etikett als Canvas (schwarz auf weiß, für Thermodruck) */
function etikettZeichnen(s, cfg = etikettCfg()) {
  const pxmm = cfg.dpi / 25.4;
  const W = Math.round(cfg.breite_mm * pxmm), H = Math.round(cfg.hoehe_mm * pxmm);
  const c = document.createElement('canvas');
  c.width = W; c.height = H;
  const g = c.getContext('2d');
  g.fillStyle = '#fff'; g.fillRect(0, 0, W, H);
  g.fillStyle = '#000'; g.textBaseline = 'top';
  const rand = Math.round(2 * pxmm);
  let rechts = W - rand;
  // QR rechts
  if (cfg.qr) {
    const q = Math.round(Math.min(H - 2 * rand, W * 0.3, H * 0.78));
    const div = document.createElement('div');
    try {
      new QRCode(div, { text: `FILAMENTLAGER:${s.uid}`, width: q, height: q, correctLevel: QRCode.CorrectLevel.M, colorDark: '#000', colorLight: '#fff' });
      const qc = div.querySelector('canvas');
      if (qc) g.drawImage(qc, W - rand - q, Math.round((H - q) / 2), q, q);
      rechts = W - rand - q - Math.round(2 * pxmm);
    } catch { /* ohne QR */ }
  }
  const breite = rechts - rand;
  const passen = (text, gewicht, start, min) => {
    let gr = start;
    for (; gr > min; gr -= 1) { g.font = `${gewicht} ${gr}px Montserrat`; if (g.measureText(text).width <= breite) break; }
    return gr;
  };
  const zeilen = [];
  const felder = new Set(cfg.felder || []);
  if (felder.has('hersteller') && s.hersteller) zeilen.push(['oben', s.hersteller.toUpperCase(), 600, 0.075]);
  zeilen.push(['gross', s.material || s.kurzname, 800, 0.2]);
  if (s.farbe) zeilen.push(['gross2', s.farbe, 700, 0.15]);
  if (s.besonderheiten.length) zeilen.push(['klein', s.besonderheiten.join(' · '), 600, 0.085]);
  const info = [];
  if (felder.has('temperatur') && s.duese_min > 0) info.push(`${s.duese_min}${s.duese_max > 0 ? '–' + s.duese_max : ''} °C`);
  if (felder.has('trocknen') && s.trocknen_c > 0) info.push(`Trocknen ${s.trocknen_c} °C${s.trocknen_h > 0 ? ' ' + s.trocknen_h + ' h' : ''}`);
  if (felder.has('hex')) info.push(s.hex.toUpperCase() + (s.fcode ? ` · ${s.fcode}` : ''));
  if (felder.has('nfc') && s.nfc[0]) info.push(`NFC ${s.nfc[0]}`);
  for (const i of info) zeilen.push(['info', i, 600, 0.082]);

  // Höhe verteilen: so groß wie möglich, ohne über den Rand zu laufen
  let faktor = 1;
  const hoehe = () => zeilen.reduce((a, z) => a + z[3] * H * faktor * 1.18, 0);
  while (hoehe() > H - 2 * rand && faktor > 0.4) faktor -= 0.04;
  let y = Math.max(rand, Math.round((H - hoehe()) / 2));
  for (const [art, text, gewicht, anteil] of zeilen) {
    const soll = Math.round(anteil * H * faktor);
    const gr = passen(text, gewicht, soll, Math.round(soll * 0.55));
    g.font = `${gewicht} ${gr}px Montserrat`;
    let t = text;
    while (g.measureText(t).width > breite && t.length > 2) t = t.slice(0, -2) + '…';
    if (art === 'info') g.fillStyle = '#222';
    g.fillText(t, rand, y);
    g.fillStyle = '#000';
    y += Math.round(anteil * H * faktor * 1.18);
  }
  return c;
}

// ------------------------------------------------------------ Phomemo per Bluetooth (ausprobieren)
function rasterBytes(canvas, cfg) {
  const druckbreite = Math.max(8, Math.floor((Number(cfg.druckbreite_punkte) || 576) / 8) * 8);
  const bpl = druckbreite / 8;
  const skal = Math.min(1, druckbreite / canvas.width);
  const w = Math.round(canvas.width * skal), h = Math.round(canvas.height * skal);
  const c = document.createElement('canvas');
  c.width = druckbreite; c.height = h;
  const g = c.getContext('2d');
  g.fillStyle = '#fff'; g.fillRect(0, 0, druckbreite, h);
  g.drawImage(canvas, Math.round((druckbreite - w) / 2), 0, w, h);
  const px = g.getImageData(0, 0, druckbreite, h).data;
  const teile = [];
  for (let y0 = 0; y0 < h; y0 += 255) {
    const zeilen = Math.min(255, h - y0);
    const b = new Uint8Array(8 + bpl * zeilen);
    b.set([0x1d, 0x76, 0x30, 0x00, bpl & 255, bpl >> 8, zeilen & 255, zeilen >> 8]);
    for (let y = 0; y < zeilen; y++) for (let x = 0; x < druckbreite; x++) {
      const i = ((y0 + y) * druckbreite + x) * 4;
      if (px[i] * 0.299 + px[i + 1] * 0.587 + px[i + 2] * 0.114 < 128) b[8 + y * bpl + (x >> 3)] |= 0x80 >> (x & 7);
    }
    teile.push(b);
  }
  return teile;
}

function phomemoDaten(canvases, cfg) {
  const teile = [Uint8Array.of(0x1b, 0x40), Uint8Array.of(0x1b, 0x4e, 0x0d, 0x03),
    Uint8Array.of(0x1b, 0x4e, 0x04, Math.max(1, Math.min(15, Number(cfg.dichte) || 8))), Uint8Array.of(0x1f, 0x11, 0x0a)];
  for (const c of canvases) {
    teile.push(...rasterBytes(c, cfg));
    teile.push(Uint8Array.of(0x1f, 0xf0, 0x05, 0x00), Uint8Array.of(0x1f, 0xf0, 0x03, 0x00));
  }
  const n = teile.reduce((a, t) => a + t.length, 0);
  const alles = new Uint8Array(n);
  let o = 0;
  for (const t of teile) { alles.set(t, o); o += t.length; }
  return alles;
}

let btGeraet = null;
async function phomemoDrucken(canvases) {
  if (!navigator.bluetooth) throw new Error('Bluetooth ist hier nicht verfügbar');
  const DIENSTE = [0xff00, 0xae30, 0x18f0, 0xfee7, 'e7810a71-73ae-499d-8c15-faa9aef0c3f2', '49535343-fe7d-4ae5-8fa9-9fafd205e455'];
  if (!btGeraet || !btGeraet.gatt) {
    btAuswahlZeigen();
    btGeraet = await navigator.bluetooth.requestDevice({ acceptAllDevices: true, optionalServices: DIENSTE });
  }
  toast(`Verbinde mit ${btGeraet.name || 'Drucker'} …`);
  const server = btGeraet.gatt.connected ? btGeraet.gatt : await btGeraet.gatt.connect();
  let ziel = null;
  for (const dienst of await server.getPrimaryServices()) {
    for (const ch of await dienst.getCharacteristics()) {
      if (ch.properties.writeWithoutResponse || ch.properties.write) { ziel = ch; break; }
    }
    if (ziel) break;
  }
  if (!ziel) throw new Error('Am Drucker keinen Schreibkanal gefunden');
  const daten = phomemoDaten(canvases, etikettCfg());
  const stueck = 100;
  for (let i = 0; i < daten.length; i += stueck) {
    const teil = daten.slice(i, i + stueck);
    if (ziel.properties.writeWithoutResponse) await ziel.writeValueWithoutResponse(teil);
    else await ziel.writeValueWithResponse(teil);
    if ((i / stueck) % 8 === 7) await new Promise((r) => setTimeout(r, 12));
  }
  return `${canvases.length} Etikett${canvases.length === 1 ? '' : 'en'} an ${btGeraet.name || 'den Drucker'} gesendet`;
}

let btListe = [];
function btAuswahlZeigen() {
  btListe = [];
  const box = dialog(`<h3>Etikettendrucker wählen</h3><span class="hinweis">Phomemo einschalten – er erscheint hier nach ein paar Sekunden (meist als „M20Pro“ oder „Q…“).</span>
    <div class="liste" id="bt-liste"><div class="leer-hinweis">Suche …</div></div>
    <div class="fuss"><button class="knopf rahmen" id="bt-ab">Abbrechen</button></div>`);
  $('#bt-ab', box).onclick = () => { dialogZu(); window.fl.bt_waehlen({ id: '' }); };
  $('#bt-liste', box).addEventListener('click', (e) => {
    const z = e.target.closest('[data-bt]'); if (!z) return;
    dialogZu(); window.fl.bt_waehlen({ id: z.dataset.bt });
  });
}
function btListeZeigen(liste) {
  btListe = liste;
  const el = $('#bt-liste'); if (!el) return;
  const sortiert = [...liste].sort((a, b) => Number(/phomemo|m20|^q\d/i.test(b.name)) - Number(/phomemo|m20|^q\d/i.test(a.name)));
  el.innerHTML = sortiert.map((g) => `<div class="wahl" data-bt="${esc(g.id)}"><span class="haken"></span><span class="wachsen" style="font-weight:600">${esc(g.name)}</span>
    ${/phomemo|m20/i.test(g.name) ? '<span class="chip g">Phomemo</span>' : ''}</div>`).join('') || '<div class="leer-hinweis">Suche …</div>';
}

// ------------------------------------------------------------ Seite
SEITEN.etiketten = {
  titel: 'Etiketten',
  zeichnen(el) {
    const cfg = etikettCfg();
    $('#titel-sub').textContent = `${cfg.breite_mm} × ${cfg.hoehe_mm} mm · ${cfg.dpi} dpi`;
    el.style.overflow = 'hidden';
    const gr = `${cfg.breite_mm}x${cfg.hoehe_mm}`;
    el.innerHTML = `<div class="etiketten">
      <div class="panel karte" style="min-height:0">
        <span class="ueber">Sorten</span>
        <label class="suche">${icon('suche', 16)}<input id="e-q" placeholder="Suchen …" value="${esc(E.q)}" autocomplete="off"></label>
        <div class="reihe mitte gap8"><span class="sub12 wachsen" id="e-zahl"></span><button class="knopf rahmen klein" id="e-keine">Keine</button></div>
        <div class="wahl-liste" id="e-liste"></div>
      </div>
      <div class="spalte gap16 minw0" style="overflow-y:auto;min-height:0">
        <div class="vorschau-flaeche" id="e-vorschau"></div>
        <div class="panel pad spalte gap12">
          <div class="reihe gap12" style="flex-wrap:wrap;align-items:flex-end">
            <div class="feld"><label>Größe</label><select class="eingabe" id="e-groesse">${GROESSEN.map(([b, h]) => `<option value="${b}x${h}" ${gr === `${b}x${h}` ? 'selected' : ''}>${b} × ${h} mm</option>`).join('')}
              ${GROESSEN.some(([b, h]) => `${b}x${h}` === gr) ? '' : `<option value="${gr}" selected>${cfg.breite_mm} × ${cfg.hoehe_mm} mm</option>`}</select></div>
            <div class="e-zeile" style="height:40px"><span>QR-Code</span><button class="schalter${cfg.qr ? ' an' : ''}" id="e-qr" aria-label="QR-Code"></button></div>
            <div class="chips" id="e-felder" style="align-self:center">${FELDER.map(([id, n]) => `<button class="filter${(cfg.felder || []).includes(id) ? ' an' : ''}" data-feld="${id}">${n}</button>`).join('')}</div>
          </div>
          <div class="trenner"></div>
          <div class="reihe gap10" style="flex-wrap:wrap">
            <button class="knopf primaer" id="e-bt">Auf Phomemo drucken (Bluetooth)</button>
            <button class="knopf rahmen" id="e-dialog">Druckdialog …</button>
            <button class="knopf rahmen" id="e-png">Als PNG sichern</button>
          </div>
          <span class="hinweis">Bluetooth-Druck ist für die Phomemo-M-Reihe gebaut, mit dem M20Pro aber noch nicht getestet. Klappt er nicht: Druckbreite und Dichte unter
            Einstellungen → Etiketten anpassen, oder das PNG mit der Phomemo-App drucken. Der QR-Code enthält die Sorte („FILAMENTLAGER:…“).</span>
        </div>
      </div></div>`;
    $('#e-q').addEventListener('input', (e) => { E.q = e.target.value; this.liste(); });
    $('#e-keine').onclick = () => { E.auswahl.clear(); this.liste(); this.vorschau(); };
    $('#e-liste').addEventListener('click', (e) => {
      const z = e.target.closest('[data-uid]'); if (!z) return;
      if (E.auswahl.has(z.dataset.uid)) E.auswahl.delete(z.dataset.uid); else E.auswahl.add(z.dataset.uid);
      this.liste(); this.vorschau();
    });
    const speichern = async (teil) => {
      const e = await aktion(() => window.fl.einst_speichern({ etikett: { ...etikettCfg(), ...teil } }), false);
      if (e) { S.einst = e; $('#titel-sub').textContent = `${e.etikett.breite_mm} × ${e.etikett.hoehe_mm} mm · ${e.etikett.dpi} dpi`; this.vorschau(); }
    };
    $('#e-groesse').onchange = (e) => { const [b, h] = e.target.value.split('x').map(Number); speichern({ breite_mm: b, hoehe_mm: h }); };
    $('#e-qr').onclick = (e) => { e.currentTarget.classList.toggle('an'); speichern({ qr: e.currentTarget.classList.contains('an') }); };
    $('#e-felder').addEventListener('click', (e) => {
      const b = e.target.closest('[data-feld]'); if (!b) return;
      b.classList.toggle('an');
      speichern({ felder: $$('#e-felder [data-feld].an').map((x) => x.dataset.feld) });
    });
    const gewaehlt = () => {
      const l = [...E.auswahl].map(sorteNach).filter(Boolean);
      if (!l.length) toast('Erst links Sorten auswählen', true);
      return l;
    };
    $('#e-bt').onclick = async () => {
      const l = gewaehlt(); if (!l.length) return;
      await aktion(() => phomemoDrucken(l.map((s) => etikettZeichnen(s))));
    };
    $('#e-dialog').onclick = async () => {
      const l = gewaehlt(); if (!l.length) return;
      const cfg2 = etikettCfg();
      await aktion(() => window.fl.etikett_drucken({ bilder: l.map((s) => etikettZeichnen(s).toDataURL('image/png')), breite_mm: cfg2.breite_mm, hoehe_mm: cfg2.hoehe_mm }), false);
    };
    $('#e-png').onclick = async () => {
      const l = gewaehlt(); if (!l.length) return;
      for (const s of l) {
        const ok = await aktion(() => window.fl.etikett_png({ dataUrl: etikettZeichnen(s).toDataURL('image/png'), name: `etikett-${s.kurzname.replace(/[^\wäöüÄÖÜß-]+/g, '-')}.png` }), false);
        if (ok === false) break;
      }
    };
    this.liste();
    document.fonts.ready.then(() => this.vorschau());
  },
  verlassen() { $('#seite').style.overflow = ''; },
  aktualisieren(art) { if (art === 'lager') { this.liste(); this.vorschau(); } },
  liste() {
    const el = $('#e-liste'); if (!el) return;
    const l = S.sorten.filter((s) => treffer(s, E.q));
    l.sort((a, b) => Number(E.auswahl.has(b.uid)) - Number(E.auswahl.has(a.uid)) || a.kurzname.localeCompare(b.kurzname, 'de'));
    $('#e-zahl').textContent = `${E.auswahl.size} ausgewählt`;
    el.innerHTML = l.slice(0, 300).map((s) => `<div class="wahl${E.auswahl.has(s.uid) ? ' an' : ''}" data-uid="${esc(s.uid)}"><span class="haken"></span>
      <div class="swatch s22" style="background:${esc(s.hex)}"></div><div class="spalte minw0 wachsen"><span class="ellipse" style="font-weight:600;font-size:13px">${esc(s.kurzname)}</span>
      <span class="sub12 ellipse">${esc(s.hersteller)}</span></div></div>`).join('');
  },
  vorschau() {
    const el = $('#e-vorschau'); if (!el) return;
    const l = [...E.auswahl].map(sorteNach).filter(Boolean);
    const beispiel = l.length ? l : S.sorten.slice(0, 1);
    el.innerHTML = '';
    const cfg = etikettCfg();
    const anzeigeBreite = Math.min(380, cfg.breite_mm * 7);
    for (const s of beispiel.slice(0, 12)) {
      const c = etikettZeichnen(s, cfg);
      c.style.width = `${anzeigeBreite}px`;
      c.style.height = `${anzeigeBreite * (cfg.hoehe_mm / cfg.breite_mm)}px`;
      if (!l.length) c.style.opacity = '.45';
      el.appendChild(c);
    }
    if (!l.length) el.insertAdjacentHTML('beforeend', '<span class="sub13" style="align-self:center">Vorschau – links Sorten auswählen</span>');
    if (l.length > 12) el.insertAdjacentHTML('beforeend', `<span class="sub13" style="align-self:center">+ ${l.length - 12} weitere</span>`);
  },
};
