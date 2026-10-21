'use strict';
/* FilamentLager Studio – gemeinsamer Zustand, Hilfen und Rahmen der Oberfläche */

const S = {
  seite: 'wiegen',
  einst: null, version: '', demo: false, plattform: '',
  sorten: [], lagerOk: false, lagerGrund: '', lagerStand: 0,
  drucker: [], ausgeblendet: [],
  waage: { gramm: 0, stabil: false, kalibriert: false, tagL: '', tagR: '', verbunden: false },
  ha: { verbunden: false, grund: '' },
  protokoll: [],
};
const SEITEN = {};

// ------------------------------------------------------------ Hilfen
const $ = (sel, wurzel = document) => wurzel.querySelector(sel);
const $$ = (sel, wurzel = document) => [...wurzel.querySelectorAll(sel)];
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const gramm = (g) => `${Math.round(g).toLocaleString('de-DE')} g`;
const kg = (g) => (g >= 1000 ? `${(g / 1000).toLocaleString('de-DE', { maximumFractionDigits: 1 })} kg` : gramm(g));
const zeit = () => new Date().toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
const sorteNach = (uid) => S.sorten.find((s) => s.uid === uid) || null;
const sorteFuerTag = (tag) => (tag ? S.sorten.find((s) => s.nfc.includes(tag)) || null : null);
const leerG = (s) => (s && s.leer_g > 0 ? s.leer_g : Number(S.einst?.waage?.leerspule_g) || 250);
const hellig = (hex) => {
  const n = parseInt((hex || '#888888').slice(1), 16);
  return (0.299 * (n >> 16) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255)) / 255;
};

const ICON = {
  wiegen: '<path d="M4 20h16"/><path d="M6 20l2-9h8l2 9"/><path d="M12 11V5"/><path d="M8 5h8"/>',
  lager: '<rect x="3" y="4" width="18" height="6" rx="1.5"/><rect x="3" y="14" width="18" height="6" rx="1.5"/><path d="M7 7h.01M7 17h.01"/>',
  drucker: '<rect x="4" y="4" width="16" height="16" rx="2.5"/><path d="M4 8.5h16"/><path d="M10.2 8.5v2.2h3.6V8.5"/><path d="M7.5 16.5h9"/>',
  etiketten: '<path d="M3 12V5a2 2 0 0 1 2-2h7l9 9-9 9-9-9z"/><circle cx="8" cy="8" r="1.5"/>',
  einst: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>',
  suche: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
  neu: '<path d="M21 12a9 9 0 1 1-3-6.7L21 8"/><path d="M21 3v5h-5"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  nfc: '<path d="M6 8.3a6 6 0 0 1 0 7.4"/><path d="M9.5 6a9.5 9.5 0 0 1 0 12"/><path d="M13 4a13 13 0 0 1 0 16"/>',
};
const icon = (n, gr = 18) => `<svg width="${gr}" height="${gr}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICON[n] || ''}</svg>`;

function toast(text, fehler = false) {
  const t = document.createElement('div');
  t.className = 'toast' + (fehler ? ' fehler' : '');
  t.textContent = text;
  $('#toasts').appendChild(t);
  setTimeout(() => t.remove(), fehler ? 6000 : 3200);
  S.protokoll.unshift({ zeit: zeit(), text, fehler });
  S.protokoll = S.protokoll.slice(0, 30);
  if (S.seite === 'wiegen' && SEITEN.wiegen.protokoll) SEITEN.wiegen.protokoll();
}

async function aktion(fn, fertigText) {
  try {
    const r = await fn();
    if (fertigText !== false) toast(typeof r === 'string' && r ? r : fertigText || 'Erledigt');
    return r;
  } catch (e) {
    toast(e.message, true);
    return undefined;
  }
}

// ------------------------------------------------------------ Dialoge
function dialogZu() { $('#dialog-ort').innerHTML = ''; }
function dialog(html, beimOeffnen) {
  const ort = $('#dialog-ort');
  ort.innerHTML = `<div class="schleier"><div class="dialog" role="dialog" aria-modal="true">${html}</div></div>`;
  const schleier = $('.schleier', ort);
  schleier.addEventListener('mousedown', (e) => { if (e.target === schleier) dialogZu(); });
  const box = $('.dialog', ort);
  if (beimOeffnen) beimOeffnen(box);
  return box;
}
document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && $('#dialog-ort').innerHTML) dialogZu(); });

function sorteZeile(s, extra = '') {
  const angebrochen = s.reste.length ? `<span class="chip o">${s.reste.length} angebrochen</span>` : '';
  return `<div class="swatch s30" style="background:${esc(s.hex)}"></div>
    <div class="spalte gap4 wachsen minw0"><span class="ellipse" style="font-weight:700">${esc(s.kurzname)}</span>
      <span class="sub12 ellipse">${esc(s.hersteller)}${s.besonderheiten.length ? ' · ' + esc(s.besonderheiten.join(', ')) : ''}</span></div>
    ${angebrochen}${extra}<span class="sub12" style="width:70px;text-align:right">${kg(s.bestand_g)}</span>`;
}

function treffer(s, q) {
  if (!q) return true;
  const t = `${s.summary} ${s.hersteller} ${s.material} ${s.farbe} ${s.besonderheiten.join(' ')} ${s.bambu_id} ${s.fcode} ${s.codes.join(' ')} ${s.nfc.join(' ')}`.toLowerCase();
  return q.toLowerCase().split(/\s+/).every((w) => t.includes(w));
}

/** Sorte aus dem Lager wählen. vorschlag: Sorte, die oben hervorgehoben wird. */
function sorteWaehlen({ titel = 'Sorte wählen', hinweis = '', vorschlag = null, knopf = 'Übernehmen' } = {}) {
  return new Promise((fertig) => {
    let gewaehlt = vorschlag ? vorschlag.uid : '';
    const box = dialog(`<h3>${esc(titel)}</h3>${hinweis ? `<span class="hinweis">${hinweis}</span>` : ''}
      <label class="suche">${icon('suche')}<input id="w-suche" placeholder="Suchen: Material, Farbe, Hersteller, Code …" autocomplete="off"></label>
      <div class="liste" id="w-liste"></div>
      <div class="fuss"><button class="knopf rahmen" id="w-ab">Abbrechen</button><button class="knopf primaer" id="w-ok" disabled>${esc(knopf)}</button></div>`);
    const zeichnen = () => {
      const q = $('#w-suche', box).value.trim();
      let liste = S.sorten.filter((s) => treffer(s, q));
      if (vorschlag && !q) liste = [vorschlag, ...liste.filter((s) => s.uid !== vorschlag.uid)];
      $('#w-liste', box).innerHTML = liste.slice(0, 250).map((s) => `<div class="wahl${s.uid === gewaehlt ? ' an' : ''}${vorschlag && s.uid === vorschlag.uid ? ' vorschlag' : ''}" data-uid="${esc(s.uid)}">
        ${sorteZeile(s, vorschlag && s.uid === vorschlag.uid ? '<span class="chip b">Vorschlag</span>' : '')}</div>`).join('') || '<div class="leer-hinweis">Nichts gefunden</div>';
      $('#w-ok', box).disabled = !gewaehlt;
    };
    $('#w-suche', box).addEventListener('input', zeichnen);
    $('#w-liste', box).addEventListener('click', (e) => {
      const z = e.target.closest('.wahl'); if (!z) return;
      gewaehlt = z.dataset.uid; zeichnen();
    });
    $('#w-liste', box).addEventListener('dblclick', (e) => {
      const z = e.target.closest('.wahl'); if (!z) return;
      dialogZu(); fertig(sorteNach(z.dataset.uid));
    });
    $('#w-ab', box).onclick = () => { dialogZu(); fertig(null); };
    $('#w-ok', box).onclick = () => { dialogZu(); fertig(sorteNach(gewaehlt)); };
    zeichnen();
    $('#w-suche', box).focus();
  });
}

function bestaetigen(titel, text, knopf = 'OK', gefaehrlich = false) {
  return new Promise((fertig) => {
    const box = dialog(`<h3>${esc(titel)}</h3><span class="hinweis">${text}</span>
      <div class="fuss"><button class="knopf rahmen" id="b-ab">Abbrechen</button><button class="knopf ${gefaehrlich ? 'rahmen rot' : 'primaer'}" id="b-ok">${esc(knopf)}</button></div>`);
    $('#b-ab', box).onclick = () => { dialogZu(); fertig(false); };
    $('#b-ok', box).onclick = () => { dialogZu(); fertig(true); };
    $('#b-ok', box).focus();
  });
}

// ------------------------------------------------------------ Rahmen
const NAVI = [['wiegen', 'Wiegen'], ['lager', 'Lager'], ['drucker', 'Drucker'], ['etiketten', 'Etiketten'], ['einst', 'Einstellungen']];

function naviZeichnen() {
  const zahl = { lager: S.sorten.length || '', drucker: S.drucker.filter((d) => !S.ausgeblendet.includes(d.id)).length || '' };
  $('#navi').innerHTML = NAVI.map(([id, name]) => `<button data-seite="${id}" class="${S.seite === id ? 'an' : ''}">${icon(id)}${name}${zahl[id] ? `<span class="zahl-klein">${zahl[id]}</span>` : ''}</button>`).join('');
}
$('#navi').addEventListener('click', (e) => {
  const b = e.target.closest('button[data-seite]');
  if (b) zeigen(b.dataset.seite);
});

function statusZeichnen() {
  const w = S.waage;
  const art = S.einst?.waage?.art;
  const waageText = art === 'aus' ? 'aus' : w.verbunden ? (art === 'pi' ? 'über Pad 7' : art === 'tcp' ? 'direkt' : 'Demo') : 'getrennt';
  const haPunkt = S.demo ? 'warn' : S.ha.verbunden ? 'an' : 'aus';
  $('#status-block').innerHTML = `
    <div class="status-zeile"><span class="dot ${haPunkt}"></span>Home Assistant<b>${S.demo ? 'Demo' : S.ha.verbunden ? 'verbunden' : 'getrennt'}</b></div>
    <div class="status-zeile"><span class="dot ${w.verbunden ? 'an' : art === 'aus' ? '' : 'aus'}"></span>Waage<b>${waageText}</b></div>
    <div class="status-zeile"><span class="dot ${w.tagL ? 'an' : ''}"></span>NFC L<span class="dot ${w.tagR ? 'an' : ''}" style="margin-left:14px"></span>NFC R</div>
    ${S.demo ? '<span class="demo-hinweis">Demo mit Beispieldaten – trag unter Einstellungen dein Home-Assistant-Token ein.</span>' : ''}
    ${!S.demo && !S.ha.verbunden && S.ha.grund ? `<span class="demo-hinweis" style="color:#ff8a80">${esc(S.ha.grund)}</span>` : ''}`;
}

function zeigen(seite) {
  if (!SEITEN[seite]) return;
  if (SEITEN[S.seite]?.verlassen) SEITEN[S.seite].verlassen();
  S.seite = seite;
  naviZeichnen();
  const p = SEITEN[seite];
  $('#titel').textContent = p.titel;
  $('#titel-sub').textContent = '';
  $('#kopf-rechts').innerHTML = '';
  $('#seite').scrollTop = 0;
  p.zeichnen($('#seite'));
}

function neuZeichnen(art) {
  naviZeichnen();
  statusZeichnen();
  const p = SEITEN[S.seite];
  if (!p) return;
  if (p.aktualisieren) p.aktualisieren(art);
  else if (art !== 'waage') p.zeichnen($('#seite'));
}

// ------------------------------------------------------------ gemeinsame Lager-Aktionen
const lagerAendern = (d) => aktion(() => window.fl.lager_aendern(d));

