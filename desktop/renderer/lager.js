'use strict';
/* Lager: alle Sorten aus todo.filament, Suche, Filter, Detail mit Bearbeiten */

const L = { q: '', filter: 'alle', material: '', spalte: 'name', auf: true, auswahl: '' };

const FILTER = [['alle', 'Alle'], ['angebrochen', 'Angebrochen'], ['leer', 'Aufgebraucht'], ['nfc', 'Mit NFC'], ['ohneplatz', 'Ohne Regalplatz']];
const SPALTEN = [
  ['name', 'Sorte', (s) => s.kurzname.toLowerCase()], ['hersteller', 'Hersteller', (s) => s.hersteller.toLowerCase()],
  ['spulen', 'Spulen', (s) => s.spulen, true], ['reste', 'Angebrochen', (s) => s.reste.length, true],
  ['bestand', 'Bestand', (s) => s.bestand_g, true], ['platz', 'Platz', (s) => (platzVon(s.uid)[0]?.text || '~')],
];

function lagerGefiltert() {
  let l = S.sorten.filter((s) => treffer(s, L.q));
  if (L.material) l = l.filter((s) => s.material === L.material);
  if (L.filter === 'angebrochen') l = l.filter((s) => s.reste.length);
  if (L.filter === 'leer') l = l.filter((s) => s.spulen === 0);
  if (L.filter === 'nfc') l = l.filter((s) => s.nfc.length);
  if (L.filter === 'ohneplatz') l = l.filter((s) => s.spulen > 0 && !platzVon(s.uid).length);
  const sp = SPALTEN.find((x) => x[0] === L.spalte) || SPALTEN[0];
  l.sort((a, b) => {
    const x = sp[2](a), y = sp[2](b);
    const r = typeof x === 'number' ? x - y : String(x).localeCompare(String(y), 'de');
    return L.auf ? r : -r;
  });
  return l;
}

SEITEN.lager = {
  titel: 'Lager',
  zeichnen(el) {
    const materialien = [...new Set(S.sorten.map((s) => s.material).filter(Boolean))].sort((a, b) => a.localeCompare(b, 'de'));
    $('#kopf-rechts').innerHTML = `<span class="sub12" id="l-stand"></span><button class="knopf rahmen klein" id="l-neu">${icon('neu', 15)}Neu laden</button>`;
    $('#l-neu').onclick = () => aktion(() => window.fl.lager_neu(), 'Lager neu geladen');
    el.style.overflow = 'hidden';
    el.innerHTML = `<div class="lager${L.auswahl ? '' : ' ohne-detail'}" id="l-raster">
      <div class="lager-links">
        <div class="werkzeug">
          <label class="suche" style="flex:1 1 260px">${icon('suche')}<input id="l-suche" placeholder="Suchen: Farbe, Material, Hersteller, Code, NFC …" value="${esc(L.q)}" autocomplete="off"></label>
          <select class="eingabe" id="l-material" style="width:150px"><option value="">Alle Materialien</option>${materialien.map((m) => `<option ${m === L.material ? 'selected' : ''}>${esc(m)}</option>`).join('')}</select>
        </div>
        <div class="werkzeug" id="l-filter">${FILTER.map(([id, n]) => `<button class="filter${L.filter === id ? ' an' : ''}" data-f="${id}">${n}</button>`).join('')}
          <span class="sub12 rechtsb" id="l-anzahl"></span></div>
        <div class="panel tabelle" id="l-tabelle"></div>
      </div>
      <div class="panel detail" id="l-detail"></div>
    </div>`;
    $('#l-suche').addEventListener('input', (e) => { L.q = e.target.value; this.tabelle(); });
    $('#l-material').onchange = (e) => { L.material = e.target.value; this.tabelle(); };
    $('#l-filter').addEventListener('click', (e) => {
      const b = e.target.closest('[data-f]'); if (!b) return;
      L.filter = b.dataset.f;
      $$('#l-filter .filter').forEach((x) => x.classList.toggle('an', x === b));
      this.tabelle();
    });
    $('#l-tabelle').addEventListener('click', (e) => {
      const th = e.target.closest('th[data-sp]');
      if (th) { if (L.spalte === th.dataset.sp) L.auf = !L.auf; else { L.spalte = th.dataset.sp; L.auf = !SPALTEN.find((x) => x[0] === th.dataset.sp)[3]; } this.tabelle(); return; }
      const tr = e.target.closest('tr[data-uid]');
      if (tr) { L.auswahl = L.auswahl === tr.dataset.uid ? '' : tr.dataset.uid; this.tabelle(); this.detail(); }
    });
    this.tabelle();
    this.detail();
    setTimeout(() => $('#l-suche')?.focus(), 0);
  },
  verlassen() { $('#seite').style.overflow = ''; },
  aktualisieren(art) {
    if (art === 'waage' || art === 'drucker') return;
    this.tabelle();
    this.detail();
  },
  tabelle() {
    const liste = lagerGefiltert();
    const stand = $('#l-stand');
    if (stand) stand.textContent = S.lagerStand ? `Stand ${new Date(S.lagerStand).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' })}` : '';
    $('#l-anzahl').textContent = `${liste.length} von ${S.sorten.length} Sorten · ${kg(liste.reduce((a, s) => a + s.bestand_g, 0))}`;
    const kopf = SPALTEN.map(([id, n, , zahl]) => `<th data-sp="${id}" class="sp-${id} ${zahl ? 'zahl-sp' : ''}">${n}${L.spalte === id ? `<span class="pfeil"> ${L.auf ? '▲' : '▼'}</span>` : ''}</th>`).join('');
    const zeilen = liste.map((s) => {
      const platz = platzVon(s.uid);
      return `<tr data-uid="${esc(s.uid)}" class="${s.uid === L.auswahl ? 'an' : ''}">
        <td><div class="reihe gap10 mitte"><div class="swatch s22" style="background:${esc(s.hex)}"></div><div class="spalte minw0">
          <span class="ellipse" style="font-weight:700;max-width:300px">${esc(s.kurzname)}</span>
          ${s.besonderheiten.length || s.hersteller ? `<span class="sub12 ellipse"><span class="nur-detail">${esc(s.hersteller)}${s.besonderheiten.length ? ' · ' : ''}</span>${esc(s.besonderheiten.join(', '))}</span>` : ''}</div>
          ${s.nfc.length ? `<span class="chip b" title="NFC-Tags">${icon('nfc', 12)} ${s.nfc.length}</span>` : ''}</div></td>
        <td class="sub13 sp-hersteller">${esc(s.hersteller)}</td>
        <td class="zahl-sp"><span class="anz${s.spulen === 0 ? ' leer' : ''}">${s.spulen}</span>${s.refills ? `<span class="sub12"> · ${s.refills} Refill</span>` : ''}</td>
        <td class="zahl-sp" style="white-space:nowrap">${s.reste.length ? s.reste.slice(0, 2).map((r) => `<span class="chip o" style="margin-left:4px">${r} g</span>`).join('') + (s.reste.length > 2 ? `<span class="sub12"> +${s.reste.length - 2}</span>` : '') : '<span class="sub12">–</span>'}</td>
        <td class="zahl-sp anz">${kg(s.bestand_g)}</td>
        <td class="sub12 ellipse sp-platz" style="max-width:170px">${platz.length ? esc(platz[0].text) + (platz.length > 1 ? ` +${platz.length - 1}` : '') : '–'}</td></tr>`;
    }).join('');
    $('#l-tabelle').innerHTML = S.sorten.length
      ? `<table><thead><tr>${kopf}</tr></thead><tbody>${zeilen || `<tr><td colspan="6" class="leer-hinweis">Keine Sorte passt zur Suche</td></tr>`}</tbody></table>`
      : `<div class="leer-hinweis">${S.lagerOk ? 'Das Lager ist leer' : esc(S.lagerGrund || 'Lager wird geladen …')}</div>`;
  },
  detail() {
    const s = sorteNach(L.auswahl);
    $('#l-raster').classList.toggle('ohne-detail', !s);
    const el = $('#l-detail');
    if (!s) { el.innerHTML = ''; return; }
    const zeile = (n, w) => (w === '' || w == null || w === -1 ? '' : `<div class="info-zeile"><span>${n}</span><span>${esc(w)}</span></div>`);
    const temp = (a, b) => (a > 0 && b > 0 ? `${a}–${b} °C` : a > 0 ? `ab ${a} °C` : '');
    const platz = platzVon(s.uid);
    el.innerHTML = `
      <div class="detail-kopf"><div class="swatch s72" style="background:${esc(s.hex)}"></div>
        <div class="spalte gap4 minw0 wachsen"><span class="ueber">${esc(s.hersteller || 'Sorte')}</span><span class="name24">${esc(s.kurzname)}</span>
          <div class="chips">${s.besonderheiten.map((b) => `<span class="chip">${esc(b)}</span>`).join('')}<span class="chip">${esc(s.hex)}</span></div></div>
        <button class="x-knopf" id="d-zu" title="Schließen" style="align-self:flex-start">✕</button></div>
      <div class="werte">
        <div class="wert"><b>${kg(s.bestand_g)}</b><span>Bestand</span></div>
        <div class="wert"><b>${s.spulen}</b><span>Spulen${s.refills ? ` · ${s.refills} Refill` : ''}</span></div>
        <div class="wert"><b>${gramm(s.gewicht)}</b><span>je Spule</span></div>
      </div>
      <div class="spalte gap8"><div class="reihe mitte"><span class="ueber">Spulen</span>
        <div class="zaehler rechtsb"><button id="d-minus" title="Eine Spule weniger">−</button><span>${s.spulen}</span><button id="d-plus" title="Eine Spule mehr">+</button></div></div></div>
      <div class="spalte gap4"><span class="ueber orange-t">Angebrochene Spulen</span>
        ${s.reste.length ? s.reste.map((r) => `<div class="rest-reihe"><span style="width:62px;font-weight:700">${r} g</span>
          <div class="balken"><div style="width:${Math.min(100, (r / s.gewicht) * 100)}%;background:${esc(s.hex)}"></div></div>
          <button class="x-knopf" data-rest="${r}" title="Rest streichen">✕</button></div>`).join('') : '<span class="sub13">Keine – alle Spulen sind voll</span>'}</div>
      <div class="raster2" style="grid-template-columns:1fr auto;align-items:end">
        <div class="feld"><label>Leere Spule wiegt (für diese Sorte)</label><input type="number" id="d-leer" min="0" max="1000" placeholder="Standard ${esc(S.einst.waage.leerspule_g)} g" value="${s.leer_g > 0 ? s.leer_g : ''}"></div>
        <button class="knopf rahmen" id="d-leer-ok">Speichern</button></div>
      <div class="spalte gap6"><span class="ueber">NFC-Tags</span>
        ${s.nfc.length ? s.nfc.map((t) => `<div class="reihe mitte gap8"><span class="chip b">${icon('nfc', 12)} ${esc(t)}</span>${s.waage[t] ? `<span class="sub12">zuletzt ${gramm(s.waage[t])}</span>` : ''}
          <button class="x-knopf rechtsb" data-tag="${esc(t)}" title="Tag entfernen">✕</button></div>`).join('') : '<span class="sub13">Noch kein Tag – Spule auf die Waage legen und zuordnen</span>'}</div>
      <div class="spalte gap6"><span class="ueber">Regalplatz</span>
        ${platz.length ? platz.map((p) => `<span class="sub13">${esc(p.text)}</span>`).join('') : '<span class="sub13">Noch keinem Regal zugeordnet</span>'}</div>
      <div class="info-liste">${zeile('Düse', temp(s.duese_min, s.duese_max))}${zeile('Bett', temp(s.bett_min, s.bett_max))}
        ${zeile('Trocknen', s.trocknen_c > 0 ? `${s.trocknen_c} °C${s.trocknen_h > 0 ? ` · ${s.trocknen_h} h` : ''}` : '')}
        ${zeile('Durchmesser', `${String(s.durchmesser).replace('.', ',')} mm`)}${zeile('Bambu-ID', s.bambu_id)}${zeile('Farbcode', s.fcode)}
        ${zeile('Barcodes', s.codes.join(', '))}${zeile('Leerspule', gramm(leerG(s)))}${zeile('Name im Lager', s.summary)}</div>
      <div class="reihe gap8" style="flex-wrap:wrap">
        <button class="knopf primaer" id="d-ams">In AMS-Slot eintragen</button>
        <button class="knopf rahmen" id="d-etikett">Etikett</button>
        <button class="knopf rahmen" id="d-regal">Regalplatz</button></div>`;
    $('#d-zu').onclick = () => { L.auswahl = ''; this.tabelle(); this.detail(); };
    $('#d-minus').onclick = () => lagerAendern({ op: 'spulen', sorte: s.uid, delta: -1 });
    $('#d-plus').onclick = () => lagerAendern({ op: 'spulen', sorte: s.uid, delta: 1 });
    $$('[data-rest]', el).forEach((b) => { b.onclick = async () => {
      if (await bestaetigen('Rest streichen?', `Den angebrochenen Rest von <b>${b.dataset.rest} g</b> bei ${esc(s.kurzname)} streichen? Die Spulenzahl bleibt gleich.`, 'Streichen', true))
        lagerAendern({ op: 'restStreichen', sorte: s.uid, gramm: Number(b.dataset.rest) });
    }; });
    $$('[data-tag]', el).forEach((b) => { b.onclick = async () => {
      if (await bestaetigen('Tag entfernen?', `Der Tag <b>${esc(b.dataset.tag)}</b> gehört dann zu keiner Sorte mehr.`, 'Entfernen', true))
        lagerAendern({ op: 'tagEntfernen', sorte: s.uid, tag: b.dataset.tag });
    }; });
    $('#d-leer-ok').onclick = () => lagerAendern({ op: 'leer', sorte: s.uid, gramm: Number($('#d-leer').value) || -1 });
    $('#d-ams').onclick = () => slotWaehlenFuer(s);
    $('#d-etikett').onclick = () => { E.auswahl = new Set([s.uid]); zeigen('etiketten'); };
    $('#d-regal').onclick = () => { R.suche = s.uid; zeigen('regale'); };
  },
};

/** Sorte in einen AMS-Slot eintragen: Drucker und Slot wählen */
function slotWaehlenFuer(s) {
  const drucker = S.drucker.filter((d) => !S.ausgeblendet.includes(d.id));
  if (!drucker.length) { toast('Keine Drucker aus Home Assistant gefunden', true); return; }
  const box = dialog(`<h3>${esc(s.kurzname)} in AMS-Slot eintragen</h3>
    <span class="hinweis">Setzt Material, Farbe und Temperaturen im Slot (bambu_lab.set_filament). Am Drucker selbst ändert sich nichts an der Spule.</span>
    <div class="liste" style="gap:14px">${drucker.map((d) => `<div class="spalte gap8"><div class="reihe mitte gap8"><b>${esc(d.name)}</b><span class="sub12">${esc(d.modell)}</span>${d.online ? '' : '<span class="status-pill aus">offline</span>'}</div>
      ${d.ams.map((a) => `<div class="ams-kopf"><b>${esc(a.name)}</b></div><div class="ams${a.slots.length === 1 ? ' eins' : ''}">${a.slots.map((sl) => slotKachel(sl, a, d)).join('')}</div>`).join('')}</div>`).join('')}</div>
    <div class="fuss"><button class="knopf rahmen" id="s-ab">Abbrechen</button></div>`);
  $('#s-ab', box).onclick = dialogZu;
  box.addEventListener('click', async (e) => {
    const k = e.target.closest('.slot[data-entity]'); if (!k) return;
    dialogZu();
    await aktion(() => window.fl.drucker_filament({ entity: k.dataset.entity, sorte: s.uid }));
  });
}
