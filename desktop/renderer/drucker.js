'use strict';
/* Drucker: Bambu-Lab-Drucker und AMS aus Home Assistant, Filament pro Slot eintragen */

const D = { alleZeigen: false, bilder: new Map() };

const STATUS = {
  running: ['Druckt', 'lauf'], pause: ['Pausiert', 'warn'], finish: ['Fertig', 'ok'], idle: ['Bereit', 'grau'],
  failed: ['Fehler', 'aus'], prepare: ['Bereitet vor', 'lauf'], slicing: ['Slicen', 'lauf'], init: ['Startet', 'grau'], offline: ['Offline', 'aus'],
};

function sorteImSlot(sl) {
  if (sl.leer) return null;
  if (sl.tag_uid) {
    const t = sl.tag_uid.toUpperCase();
    const s = S.sorten.find((x) => x.nfc.some((n) => n.toUpperCase().startsWith(t.slice(0, 8)) || t.startsWith(n.toUpperCase().slice(0, 8))));
    if (s) return s;
  }
  const v = vorschlagFuerSlot(sl);
  if (!v) return null;
  const n = (h) => parseInt(h.slice(1), 16);
  const abst = Math.abs((n(v.hex) >> 16) - (n(sl.hex) >> 16)) + Math.abs(((n(v.hex) >> 8) & 255) - ((n(sl.hex) >> 8) & 255)) + Math.abs((n(v.hex) & 255) - (n(sl.hex) & 255));
  return abst < 60 ? v : null;
}

function slotKachel(sl, a, d) {
  const s = sorteImSlot(sl);
  const unter = sl.leer ? 'leer' : s ? s.kurzname : 'nicht im Lager';
  return `<button class="slot${sl.leer ? ' leer' : ''}${sl.aktiv ? ' aktiv' : ''}" data-entity="${esc(sl.entity)}" data-drucker="${esc(d.id)}" title="${esc(sl.name || 'leer')}">
    <div class="reihe mitte gap8"><div class="spule-klein" style="background:${sl.leer ? 'transparent' : esc(sl.hex)}"></div>
      <b>${esc(a.kurz)}${a.slots.length > 1 ? sl.slot : ''}${sl.aktiv ? ' · aktiv' : ''}</b></div>
    <small>${esc(sl.leer ? '—' : sl.name || sl.typ)}</small>
    <small class="${sl.leer ? '' : s ? 'treffer' : 'kein'}">${esc(unter)}${!sl.leer && sl.rest >= 0 ? ` · ${sl.rest} %` : ''}</small></button>`;
}

function dauer(min) {
  if (min == null) return '';
  const h = Math.floor(min / 60), m = Math.round(min % 60);
  return h ? `${h} h ${m} min` : `${m} min`;
}

SEITEN.drucker = {
  titel: 'Drucker',
  zeichnen(el) {
    $('#titel-sub').textContent = 'über die Bambu-Lab-Integration in Home Assistant';
    $('#kopf-rechts').innerHTML = `<button class="knopf rahmen klein" id="d-alle"></button><button class="knopf rahmen klein" id="d-neu">${icon('neu', 15)}Neu laden</button>`;
    $('#d-neu').onclick = () => aktion(() => window.fl.drucker_neu(), 'Drucker neu geladen');
    $('#d-alle').onclick = () => { D.alleZeigen = !D.alleZeigen; this.aktualisieren(); };
    el.innerHTML = '<div class="drucker-raster" id="d-raster"></div>';
    $('#d-raster').addEventListener('click', (e) => {
      const sl = e.target.closest('.slot[data-entity]');
      if (sl) return slotMenue(sl.dataset.drucker, sl.dataset.entity);
      const m = e.target.closest('[data-menue]');
      if (m) druckerMenue(m.dataset.menue);
    });
    this.aktualisieren();
  },
  aktualisieren(art) {
    if (art === 'waage') return;
    const raster = $('#d-raster'); if (!raster) return;
    const versteckt = S.drucker.filter((d) => S.ausgeblendet.includes(d.id)).length;
    $('#d-alle').textContent = D.alleZeigen ? 'Ausgeblendete verbergen' : `Ausgeblendete zeigen${versteckt ? ` (${versteckt})` : ''}`;
    $('#d-alle').classList.toggle('versteckt', !versteckt);
    const liste = S.drucker.filter((d) => D.alleZeigen || !S.ausgeblendet.includes(d.id));
    if (!liste.length) {
      raster.innerHTML = `<div class="panel pad leer-hinweis" style="grid-column:1/-1">${S.demo || S.ha.verbunden
        ? 'Keine Bambu-Lab-Drucker gefunden. Ist die Integration „Bambu Lab“ (ha-bambulab) in Home Assistant eingerichtet?'
        : 'Home Assistant ist nicht verbunden – die Drucker kommen von dort.'}</div>`;
      return;
    }
    raster.innerHTML = liste.map((d) => {
      const [stText, stArt] = STATUS[d.status] || [d.status, 'grau'];
      const druckt = ['running', 'pause', 'prepare'].includes(d.status);
      const bild = D.bilder.get(d.id);
      if (d.titelbild && (!bild || bild.stand !== d.titelbild_stand)) bildHolen(d);
      const ende = d.endzeit && !Number.isNaN(Date.parse(d.endzeit)) ? new Date(d.endzeit).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' }) : '';
      const t = (n, w, z) => (w == null ? '' : `<span>${n} <b>${Math.round(w)}°</b>${z ? `<span class="sub12">/${Math.round(z)}°</span>` : ''}</span>`);
      return `<section class="panel drucker${d.online ? '' : ' offline'}${S.ausgeblendet.includes(d.id) ? ' offline' : ''}">
        <div class="drucker-kopf">
          <div class="drucker-bild">${bild?.url ? `<img src="${bild.url}" alt="">` : icon('drucker', 30)}</div>
          <div class="spalte gap4 wachsen minw0"><div class="reihe mitte gap8"><span class="name17 ellipse">${esc(d.name)}</span><span class="status-pill ${stArt}">${esc(stText)}</span></div>
            <span class="sub12 ellipse">${esc(d.modell)} · …${esc(d.id.slice(-4))}${d.auftrag ? ` · ${esc(d.auftrag)}` : ''}</span>
            <div class="temps">${t('Düse', d.duese, d.duese_ziel)}${d.duese_rechts != null ? t('rechts', d.duese_rechts) : ''}${t('Bett', d.bett, d.bett_ziel)}${t('Raum', d.raum)}</div></div>
          <button class="knopf icon rahmen" data-menue="${esc(d.id)}" title="Name und Anzeige">⋯</button>
        </div>
        ${druckt && d.fortschritt != null ? `<div class="spalte gap6"><div class="reihe mitte sub12"><b style="color:var(--text);font-size:13px">${Math.round(d.fortschritt)} %</b>
          <span style="margin-left:10px">${d.schicht != null && d.schichten ? `Schicht ${d.schicht}/${d.schichten}` : ''}</span>
          <span class="rechtsb">${d.rest_min ? `noch ${dauer(d.rest_min)}` : ''}${ende ? ` · fertig ${ende}` : ''}</span></div>
          <div class="balken"><div style="width:${Math.max(0, Math.min(100, d.fortschritt))}%"></div></div></div>` : ''}
        ${d.ams.map((a) => `<div class="ams-block"><div class="ams-kopf"><b>${esc(a.name)}</b>
            ${a.feuchte != null ? `<span>· ${a.feuchte} % Feuchte</span>` : ''}${a.temperatur != null ? `<span>· ${a.temperatur} °C</span>` : ''}</div>
          <div class="ams${a.slots.length === 1 ? ' eins' : ''}">${a.slots.map((sl) => slotKachel(sl, a, d)).join('')}</div></div>`).join('')}
      </section>`;
    }).join('');
  },
};

async function bildHolen(d) {
  D.bilder.set(d.id, { stand: d.titelbild_stand, url: D.bilder.get(d.id)?.url || '' });
  try {
    const url = await window.fl.drucker_bild({ pfad: d.titelbild });
    D.bilder.set(d.id, { stand: d.titelbild_stand, url: url || '' });
    if (url && S.seite === 'drucker') SEITEN.drucker.aktualisieren();
  } catch { /* ohne Bild */ }
}

function slotFinden(dId, entity) {
  const d = S.drucker.find((x) => x.id === dId);
  for (const a of d?.ams || []) for (const sl of a.slots) if (sl.entity === entity) return { d, a, sl };
  return null;
}

function slotMenue(dId, entity) {
  const f = slotFinden(dId, entity); if (!f) return;
  const { d, a, sl } = f;
  const s = sorteImSlot(sl);
  const box = dialog(`<h3>${esc(d.name)} · ${esc(a.name)}${a.slots.length > 1 ? ` · Slot ${sl.slot}` : ''}</h3>
    <div class="reihe gap12 mitte"><div class="swatch s44" style="background:${sl.leer ? 'var(--rand)' : esc(sl.hex)}"></div>
      <div class="spalte gap4 minw0"><span class="name17">${esc(sl.leer ? 'Leer' : sl.name || sl.typ)}</span>
      <span class="sub13">${sl.leer ? 'Kein Filament im Slot' : [sl.typ, sl.filament_id, sl.rest >= 0 ? `${sl.rest} % laut AMS` : '', sl.tag_uid ? `Tag ${sl.tag_uid}` : ''].filter(Boolean).map(esc).join(' · ')}</span></div></div>
    ${s ? `<div class="panel karte" style="padding:12px 14px"><span class="ueber">Im Lager</span><div class="reihe gap10 mitte">${sorteZeile(s)}</div></div>` : sl.leer ? '' : '<span class="hinweis">Zu diesem Slot gibt es keine passende Sorte im Lager.</span>'}
    <div class="fuss">${!a.extern && !sl.leer ? '<button class="knopf rahmen" id="m-rfid">Tag neu lesen</button>' : ''}
      ${s ? '<button class="knopf rahmen" id="m-lager">Im Lager zeigen</button>' : ''}
      <button class="knopf primaer" id="m-setzen">Sorte eintragen …</button></div>`);
  if ($('#m-rfid', box)) $('#m-rfid', box).onclick = () => { dialogZu(); aktion(() => window.fl.drucker_rfid({ entity })); };
  if ($('#m-lager', box)) $('#m-lager', box).onclick = () => { dialogZu(); L.auswahl = s.uid; zeigen('lager'); };
  $('#m-setzen', box).onclick = async () => {
    dialogZu();
    const neu = await sorteWaehlen({ titel: `Sorte für ${a.name}${a.slots.length > 1 ? ` Slot ${sl.slot}` : ''}`, hinweis: 'Material, Farbe und Düsentemperaturen werden im Slot gesetzt.', vorschlag: s || (sl.leer ? null : vorschlagFuerSlot(sl)), knopf: 'Eintragen' });
    if (neu) aktion(() => window.fl.drucker_filament({ entity, sorte: neu.uid }));
  };
}

function druckerMenue(id) {
  const d = S.drucker.find((x) => x.id === id); if (!d) return;
  const versteckt = S.ausgeblendet.includes(id);
  const box = dialog(`<h3>${esc(d.modell)} …${esc(id.slice(-4))}</h3>
    <div class="feld"><label>Name in der App</label><input id="dm-name" value="${esc(S.einst.drucker.namen[id] || '')}" placeholder="${esc(d.name)}"></div>
    <div class="e-zeile"><span class="wachsen">In der Übersicht ausblenden</span><button class="schalter${versteckt ? ' an' : ''}" id="dm-aus" aria-label="Ausblenden"></button></div>
    <span class="hinweis">Seriennummer ${esc(id)}</span>
    <div class="fuss"><button class="knopf rahmen" id="dm-ab">Abbrechen</button><button class="knopf primaer" id="dm-ok">Speichern</button></div>`);
  $('#dm-aus', box).onclick = (e) => e.currentTarget.classList.toggle('an');
  $('#dm-ab', box).onclick = dialogZu;
  $('#dm-ok', box).onclick = async () => {
    const namen = { ...S.einst.drucker.namen };
    const n = $('#dm-name', box).value.trim();
    if (n) namen[id] = n; else delete namen[id];
    const aus = new Set(S.ausgeblendet);
    if ($('#dm-aus', box).classList.contains('an')) aus.add(id); else aus.delete(id);
    dialogZu();
    const e = await aktion(() => window.fl.einst_speichern({ drucker: { namen, ausgeblendet: [...aus] } }), 'Gespeichert');
    if (e) S.einst = e;
  };
}
