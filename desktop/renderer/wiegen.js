'use strict';
/* Wiegen: Live-Gewicht von der Waage, Spule am NFC-Tag erkennen, Rest ins Lager schreiben */

const W = { wahl: null, leerSeit: 0 };

function amsSlotFuerTag(tag) {
  if (!tag) return null;
  const t = tag.toUpperCase();
  for (const d of S.drucker) for (const a of d.ams) for (const sl of a.slots) {
    const u = (sl.tag_uid || '').toUpperCase();
    if (u && (u.startsWith(t.slice(0, 8)) || t.startsWith(u.slice(0, 8)))) return { drucker: d, ams: a, slot: sl };
  }
  return null;
}
function vorschlagFuerSlot(sl) {
  const kand = S.sorten.filter((s) => s.bambu_id && s.bambu_id === sl.filament_id);
  if (!kand.length) return null;
  const n = (h) => parseInt(h.slice(1), 16);
  const abst = (a, b) => Math.abs((n(a) >> 16) - (n(b) >> 16)) + Math.abs(((n(a) >> 8) & 255) - ((n(b) >> 8) & 255)) + Math.abs((n(a) & 255) - (n(b) & 255));
  return kand.sort((x, y) => abst(x.hex, sl.hex) - abst(y.hex, sl.hex))[0];
}

function wiegenLage() {
  const w = S.waage;
  const tag = w.tagR || w.tagL;
  const bekannt = sorteFuerTag(tag);
  const sorte = bekannt || W.wahl;
  const leer = leerG(sorte);
  const netto = Math.max(0, Math.round(w.gramm - leer));
  const belegt = w.gramm > 30;
  return { w, tag, bekannt, sorte, leer, netto, belegt };
}

SEITEN.wiegen = {
  titel: 'Wiegen',
  zeichnen(el) {
    $('#titel-sub').textContent = 'Spule auflegen – die Waage erkennt sie am NFC-Tag';
    el.innerHTML = `
      <div class="wiegen">
        <div class="spalte gap20 minw0">
          <section class="panel gruen wiegen-karte" id="w-karte">
            <div class="ring" id="w-ring"><div class="ring-innen"><div class="spule" id="w-spule"><div class="nabe"></div></div></div></div>
            <div class="spalte gap8 minw0">
              <span class="ueber">Filament übrig</span>
              <div class="zahl-zeile"><span class="zahl" id="w-zahl">0</span><span class="einheit">g</span></div>
              <span class="sub13" id="w-proz" style="font-size:15px">Spule auflegen</span>
              <div class="chips" style="margin-top:10px"><span class="chip" id="w-brutto">Brutto 0 g</span><span class="chip" id="w-leer">Leerspule 250 g</span><span class="chip" id="w-ruhig"></span></div>
            </div>
          </section>
          <div class="kacheln" id="w-kacheln"></div>
        </div>
        <aside class="aktion-spalte">
          <div class="panel gruen karte" id="w-sorte"></div>
          <button class="knopf primaer gross" id="w-haupt" disabled>Rest speichern</button>
          <div class="reihe gap10"><button class="knopf rahmen wachsen" id="w-tara">Tara</button><button class="knopf rahmen wachsen" id="w-wahl">Sorte wählen</button></div>
          <div class="panel karte protokoll"><span class="ueber">Zuletzt</span><div id="w-protokoll"></div></div>
        </aside>
      </div>`;
    $('#w-tara').onclick = () => aktion(() => window.fl.waage_tara(), false);
    $('#w-wahl').onclick = async () => {
      const l = wiegenLage();
      const ams = amsSlotFuerTag(l.tag);
      const s = await sorteWaehlen({
        titel: l.tag && !l.bekannt ? 'Zu welcher Sorte gehört diese Spule?' : 'Sorte für diese Spule',
        hinweis: l.tag && !l.bekannt ? `Neuer Tag <b>${esc(l.tag)}</b> – er wird der gewählten Sorte zugeordnet.` : 'Ohne Tag gilt die Wahl, bis der Teller wieder leer ist.',
        vorschlag: ams ? vorschlagFuerSlot(ams.slot) : l.sorte,
        knopf: l.tag && !l.bekannt ? 'Zuordnen' : 'Übernehmen',
      });
      if (!s) return;
      if (l.tag && !l.bekannt) await lagerAendern({ op: 'zuordnen', sorte: s.uid, tag: l.tag });
      else W.wahl = s;
      this.aktualisieren('waage');
    };
    $('#w-haupt').onclick = async () => {
      const l = wiegenLage();
      if (l.tag && !l.bekannt) return $('#w-wahl').click();
      if (!l.sorte || !l.w.stabil) return;
      const knopf = $('#w-haupt');
      knopf.disabled = true;
      await lagerAendern({ op: 'rest', sorte: l.sorte.uid, netto: l.netto, tag: l.bekannt ? l.tag : '' });
      knopf.disabled = false;
    };
    this.kacheln();
    this.protokoll();
    this.aktualisieren('waage');
  },
  kacheln() {
    const k = $('#w-kacheln'); if (!k) return;
    const sorten = S.sorten;
    const spulen = sorten.reduce((a, s) => a + s.spulen, 0);
    const angebrochen = sorten.reduce((a, s) => a + s.reste.length, 0);
    const bestand = sorten.reduce((a, s) => a + s.bestand_g, 0);
    const mitTag = sorten.filter((s) => s.nfc.length).length;
    k.innerHTML = [[kg(bestand), 'Filament im Lager'], [spulen, `Spulen in ${sorten.length} Sorten`], [angebrochen, 'angebrochene Spulen'], [mitTag, 'Sorten mit NFC-Tag']]
      .map(([b, t]) => `<div class="kachel"><b>${b}</b><span>${t}</span></div>`).join('');
  },
  protokoll() {
    const p = $('#w-protokoll'); if (!p) return;
    p.innerHTML = S.protokoll.slice(0, 7).map((m) => `<div class="p-zeile${m.fehler ? ' fehler' : ''}"><span class="p-zeit">${m.zeit}</span><span>${esc(m.text)}</span></div>`).join('') ||
      '<div class="sub13" style="padding:8px 0">Noch nichts gewogen</div>';
  },
  aktualisieren(art) {
    if (art === 'lager') this.kacheln();
    const l = wiegenLage();
    const { w, tag, bekannt, sorte } = l;
    if (!l.belegt) {
      if (W.wahl && Date.now() - W.leerSeit > 1500 && W.leerSeit) W.wahl = null;
      if (!W.leerSeit) W.leerSeit = Date.now();
    } else W.leerSeit = 0;

    const ziel = sorte?.gewicht || 1000;
    const proz = sorte && l.belegt ? Math.max(0, Math.min(100, Math.round((l.netto / ziel) * 100))) : 0;
    const farbe = sorte?.hex || '#3a4048';
    $('#w-zahl').textContent = l.belegt ? l.netto.toLocaleString('de-DE') : '0';
    $('#w-karte').classList.toggle('unruhig', l.belegt && !w.stabil);
    $('#w-ring').style.background = `conic-gradient(${sorte ? 'var(--gruen)' : 'var(--rand)'} 0 ${proz}%, var(--rand2) ${proz}% 100%)`;
    $('#w-spule').style.background = l.belegt ? farbe : 'var(--rand)';
    $('#w-proz').textContent = !w.verbunden ? (S.einst?.waage?.art === 'aus' ? 'Keine Waage eingerichtet' : 'Waage nicht verbunden')
      : !l.belegt ? 'Spule auflegen' : sorte ? `${proz} % von ${gramm(ziel)}` : 'Sorte unbekannt';
    $('#w-brutto').textContent = `Brutto ${gramm(Math.max(0, w.gramm))}`;
    $('#w-leer').textContent = `Leerspule ${gramm(l.leer)}`;
    const ruhig = $('#w-ruhig');
    ruhig.textContent = !w.verbunden ? 'getrennt' : !w.kalibriert ? 'nicht kalibriert' : w.stabil ? 'ruhig' : 'schwankt …';
    ruhig.className = 'chip ' + (!w.verbunden || !w.kalibriert ? 'r' : w.stabil ? 'g' : 'o');

    const ams = amsSlotFuerTag(tag);
    let kopf, name, unter, chips = [];
    if (bekannt) {
      kopf = 'Erkannt am NFC-Tag'; name = bekannt.kurzname; unter = bekannt.hersteller;
      chips.push(`<span class="chip g">${esc(tag)}</span>`);
    } else if (tag) {
      kopf = 'Neuer NFC-Tag'; name = 'Unbekannte Spule'; unter = 'Einmal zuordnen, dann erkennt die Waage sie';
      chips.push(`<span class="chip o">${esc(tag)}</span>`);
      if (ams) { const v = vorschlagFuerSlot(ams.slot); if (v) chips.push(`<span class="chip b">Vorschlag: ${esc(v.kurzname)}</span>`); }
    } else if (W.wahl && l.belegt) {
      kopf = 'Von Hand gewählt'; name = W.wahl.kurzname; unter = W.wahl.hersteller;
    } else {
      kopf = 'Keine Spule erkannt'; name = l.belegt ? 'Spule ohne Tag' : 'Teller ist leer'; unter = l.belegt ? 'Sorte von Hand wählen' : 'Spule mit Tag auflegen';
    }
    if (ams) chips.push(`<span class="chip b">${esc(ams.drucker.name)} · ${esc(ams.ams.kurz)}${ams.ams.slots.length > 1 ? ams.slot.slot : ''}${ams.slot.rest >= 0 ? ` · ${ams.slot.rest} %` : ''}</span>`);
    if (sorte) {
      if (sorte.reste.length) chips.push(`<span class="chip o">Reste ${sorte.reste.map(gramm).join(', ')}</span>`);
      chips.push(`<span class="chip">${sorte.spulen} Spule${sorte.spulen === 1 ? '' : 'n'}</span>`);
      for (const p of platzVon(sorte.uid).slice(0, 1)) chips.push(`<span class="chip">${esc(p.text)}</span>`);
    }
    $('#w-sorte').innerHTML = `<span class="ueber ellipse">${esc(kopf)}</span>
      <div class="reihe gap12 mitte"><div class="swatch s44" style="background:${sorte ? esc(sorte.hex) : 'var(--rand)'}"></div>
        <div class="spalte gap4 minw0"><span class="name17 ellipse">${esc(name)}</span><span class="sub13 ellipse">${esc(unter)}</span></div></div>
      <div class="chips">${chips.join('')}</div>`;

    const haupt = $('#w-haupt');
    if (tag && !bekannt) { haupt.textContent = 'Sorte zuordnen'; haupt.disabled = !S.sorten.length; }
    else {
      haupt.textContent = l.netto <= 10 && l.belegt ? 'Spule leer – ausbuchen' : 'Rest speichern';
      haupt.disabled = !(sorte && l.belegt && w.stabil && w.verbunden);
    }
    $('#w-wahl').textContent = tag && !bekannt ? 'Sorte suchen' : 'Sorte wählen';
  },
};
