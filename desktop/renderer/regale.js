'use strict';
/* Regale: Spulen physischen Plätzen zuordnen (gespeichert auf dem Mac, als Datei sicherbar) */

const R = { aktiv: '', suche: '', q: '' };

const neueId = () => 'r' + Date.now().toString(36) + Math.random().toString(36).slice(2, 5);

function regalDialog(regal) {
  const neu = !regal;
  const r = regal || { id: neueId(), name: `Regal ${S.regale.regale.length + 1}`, unter: '', ebenen: 4, plaetze: 6 };
  const box = dialog(`<h3>${neu ? 'Neues Regal' : 'Regal bearbeiten'}</h3>
    <div class="raster2"><div class="feld"><label>Name</label><input id="rg-name" value="${esc(r.name)}"></div>
      <div class="feld"><label>Ort / Beschreibung</label><input id="rg-unter" value="${esc(r.unter || '')}" placeholder="z. B. Keller links"></div>
      <div class="feld"><label>Ebenen</label><input id="rg-e" type="number" min="1" max="12" value="${r.ebenen}"></div>
      <div class="feld"><label>Plätze je Ebene</label><input id="rg-p" type="number" min="1" max="20" value="${r.plaetze}"></div></div>
    <span class="hinweis">Wird ein Regal kleiner, fallen Spulen auf wegfallenden Plätzen zurück in die Ablage.</span>
    <div class="fuss">${neu ? '' : '<button class="knopf rahmen rot" id="rg-weg" style="margin-right:auto">Regal löschen</button>'}
      <button class="knopf rahmen" id="rg-ab">Abbrechen</button><button class="knopf primaer" id="rg-ok">${neu ? 'Anlegen' : 'Speichern'}</button></div>`);
  $('#rg-ab', box).onclick = dialogZu;
  $('#rg-ok', box).onclick = async () => {
    r.name = $('#rg-name', box).value.trim() || r.name;
    r.unter = $('#rg-unter', box).value.trim();
    r.ebenen = Math.max(1, Math.min(12, Number($('#rg-e', box).value) || 1));
    r.plaetze = Math.max(1, Math.min(20, Number($('#rg-p', box).value) || 1));
    if (neu) S.regale.regale.push(r);
    for (const k of Object.keys(S.regale.plaetze)) {
      const [rid, e, p] = k.split('/');
      if (rid === r.id && (Number(e) >= r.ebenen || Number(p) >= r.plaetze)) delete S.regale.plaetze[k];
    }
    R.aktiv = r.id;
    dialogZu();
    await regaleSpeichern();
    SEITEN.regale.zeichnen($('#seite'));
  };
  if ($('#rg-weg', box)) $('#rg-weg', box).onclick = async () => {
    dialogZu();
    if (!(await bestaetigen('Regal löschen?', `<b>${esc(r.name)}</b> mit allen Platzzuordnungen löschen? Im Lager ändert sich nichts.`, 'Löschen', true))) return;
    S.regale.regale = S.regale.regale.filter((x) => x.id !== r.id);
    for (const k of Object.keys(S.regale.plaetze)) if (k.startsWith(r.id + '/')) delete S.regale.plaetze[k];
    R.aktiv = S.regale.regale[0]?.id || '';
    await regaleSpeichern();
    SEITEN.regale.zeichnen($('#seite'));
  };
  $('#rg-name', box).select();
}

async function platzSetzen(key, uid) {
  if (uid) S.regale.plaetze[key] = { sorte: uid };
  else delete S.regale.plaetze[key];
  await regaleSpeichern();
  SEITEN.regale.aktualisieren();
}

SEITEN.regale = {
  titel: 'Regale',
  zeichnen(el) {
    $('#titel-sub').textContent = 'Spulen auf Plätze ziehen – so weißt du, wo jede Rolle liegt';
    $('#kopf-rechts').innerHTML = `<button class="knopf rahmen klein" id="rg-import">Laden …</button><button class="knopf rahmen klein" id="rg-export">Sichern …</button>`;
    $('#rg-export').onclick = () => aktion(() => window.fl.regale_export(S.regale), 'Regale gesichert');
    $('#rg-import').onclick = async () => {
      const d = await aktion(() => window.fl.regale_import(), false);
      if (!d) return;
      if (!(await bestaetigen('Regale ersetzen?', `Die Datei enthält ${d.regale.length} Regal(e). Die jetzigen Regale werden ersetzt.`, 'Ersetzen', true))) return;
      S.regale = d; R.aktiv = d.regale[0]?.id || '';
      await regaleSpeichern(); this.zeichnen($('#seite'));
    };
    if (!S.regale.regale.find((r) => r.id === R.aktiv)) R.aktiv = S.regale.regale[0]?.id || '';
    if (R.suche) {
      const p = platzVon(R.suche)[0];
      if (p) R.aktiv = p.regal.id;
    }
    el.style.overflow = 'hidden';
    el.innerHTML = `<div class="regale">
      <div class="panel regal-liste" id="rg-liste"></div>
      <div class="panel regal-ansicht" id="rg-ansicht"></div>
      <div class="panel ablage"><span class="ueber">Ablage</span>
        <label class="suche">${icon('suche', 16)}<input id="rg-q" placeholder="Sorte suchen …" value="${esc(R.q)}" autocomplete="off"></label>
        <span class="sub12" id="rg-ablage-info"></span>
        <div class="ablage-liste" id="rg-ablage"></div></div>
    </div>`;
    $('#rg-q').addEventListener('input', (e) => { R.q = e.target.value; this.ablage(); });
    $('#rg-liste').addEventListener('click', (e) => {
      if (e.target.closest('#rg-neu')) return regalDialog(null);
      const b = e.target.closest('[data-regal]'); if (!b) return;
      R.aktiv = b.dataset.regal; this.aktualisieren();
    });
    const ansicht = $('#rg-ansicht');
    ansicht.addEventListener('click', async (e) => {
      if (e.target.closest('#rg-bearb')) return regalDialog(S.regale.regale.find((r) => r.id === R.aktiv));
      if (e.target.closest('#rg-erstes')) return regalDialog(null);
      const p = e.target.closest('.platz'); if (!p) return;
      const belegt = S.regale.plaetze[p.dataset.key];
      const s = belegt && sorteNach(belegt.sorte);
      if (!s) {
        const neu = await sorteWaehlen({ titel: 'Welche Sorte liegt hier?', vorschlag: R.suche ? sorteNach(R.suche) : null });
        if (neu) { R.suche = ''; platzSetzen(p.dataset.key, neu.uid); }
        return;
      }
      const box = dialog(`<h3>${esc(p.title)}</h3><div class="reihe gap10 mitte">${sorteZeile(s)}</div>
        <div class="fuss"><button class="knopf rahmen rot" id="pl-weg" style="margin-right:auto">Platz leeren</button>
          <button class="knopf rahmen" id="pl-tausch">Andere Sorte …</button><button class="knopf rahmen" id="pl-etikett">Etikett</button><button class="knopf primaer" id="pl-lager">Im Lager zeigen</button></div>`);
      $('#pl-weg', box).onclick = () => { dialogZu(); platzSetzen(p.dataset.key, ''); };
      $('#pl-etikett', box).onclick = () => { dialogZu(); E.auswahl = new Set([s.uid]); zeigen('etiketten'); };
      $('#pl-lager', box).onclick = () => { dialogZu(); L.auswahl = s.uid; zeigen('lager'); };
      $('#pl-tausch', box).onclick = async () => {
        dialogZu();
        const neu = await sorteWaehlen({ titel: 'Welche Sorte liegt hier?', vorschlag: s });
        if (neu) platzSetzen(p.dataset.key, neu.uid);
      };
    });
    // Ziehen: aus der Ablage oder von Platz zu Platz
    const start = (e) => {
      const z = e.target.closest('[data-zieh]'); if (!z) return;
      e.dataTransfer.setData('text/plain', JSON.stringify({ sorte: z.dataset.zieh, von: z.dataset.key || '' }));
      e.dataTransfer.effectAllowed = 'move';
    };
    el.addEventListener('dragstart', start);
    ansicht.addEventListener('dragover', (e) => { const p = e.target.closest('.platz'); if (p) { e.preventDefault(); p.classList.add('drueber'); } });
    ansicht.addEventListener('dragleave', (e) => { const p = e.target.closest('.platz'); if (p) p.classList.remove('drueber'); });
    ansicht.addEventListener('drop', async (e) => {
      const p = e.target.closest('.platz'); if (!p) return;
      e.preventDefault();
      let d; try { d = JSON.parse(e.dataTransfer.getData('text/plain')); } catch { return; }
      const alt = S.regale.plaetze[p.dataset.key];
      if (d.von && d.von !== p.dataset.key) {
        if (alt) S.regale.plaetze[d.von] = alt; else delete S.regale.plaetze[d.von];
      }
      R.suche = '';
      await platzSetzen(p.dataset.key, d.sorte);
    });
    $('#rg-ablage').addEventListener('dragover', (e) => e.preventDefault());
    $('#rg-ablage').addEventListener('drop', (e) => {
      e.preventDefault();
      let d; try { d = JSON.parse(e.dataTransfer.getData('text/plain')); } catch { return; }
      if (d.von) platzSetzen(d.von, '');
    });
    this.aktualisieren();
  },
  verlassen() { $('#seite').style.overflow = ''; R.suche = ''; },
  aktualisieren(art) {
    if (art === 'waage' || art === 'drucker' || !$('#rg-liste')) return;
    const belegtZahl = (r) => Object.keys(S.regale.plaetze).filter((k) => k.startsWith(r.id + '/')).length;
    $('#rg-liste').innerHTML = S.regale.regale.map((r) => `<button class="regal-eintrag${r.id === R.aktiv ? ' an' : ''}" data-regal="${esc(r.id)}">
        <span class="wachsen minw0"><b class="ellipse">${esc(r.name)}</b><small>${belegtZahl(r)} von ${r.ebenen * r.plaetze} belegt${r.unter ? ` · ${esc(r.unter)}` : ''}</small></span></button>`).join('') +
      `<button class="knopf rahmen" id="rg-neu" style="margin-top:6px">${icon('plus', 15)}Neues Regal</button>`;
    const r = S.regale.regale.find((x) => x.id === R.aktiv);
    const ans = $('#rg-ansicht');
    if (!r) {
      ans.innerHTML = `<div class="leer-hinweis spalte gap12 mitte" style="margin:auto"><span class="name17">Noch kein Regal</span>
        <span class="sub13">Leg dein erstes Regal an – Ebenen und Plätze wie in echt.</span><button class="knopf primaer" id="rg-erstes">${icon('plus', 15)}Regal anlegen</button></div>`;
    } else {
      let ebenen = '';
      for (let e = 0; e < r.ebenen; e++) {
        let plaetze = '';
        for (let p = 0; p < r.plaetze; p++) {
          const key = `${r.id}/${e}/${p}`;
          const s = S.regale.plaetze[key] && sorteNach(S.regale.plaetze[key].sorte);
          const titel = `${r.name} · Ebene ${e + 1} · Platz ${p + 1}`;
          plaetze += s
            ? `<button class="platz voll${R.suche === s.uid ? ' gesucht' : ''}" draggable="true" data-zieh="${esc(s.uid)}" data-key="${key}" title="${esc(titel)}: ${esc(s.kurzname)}" style="background:${esc(s.hex)}">${s.reste.length ? `<span class="nr">${s.reste.length}×</span>` : ''}</button>`
            : `<button class="platz" data-key="${key}" title="${esc(titel)}">${p + 1}</button>`;
        }
        ebenen += `<div class="spalte gap4"><span class="sub12">Ebene ${e + 1}</span><div class="ebene" style="grid-template-columns:repeat(${r.plaetze}, minmax(0, 1fr))">${plaetze}</div></div>`;
      }
      const sorten = new Set(Object.entries(S.regale.plaetze).filter(([k]) => k.startsWith(r.id + '/')).map(([, v]) => v.sorte));
      ans.innerHTML = `<div class="reihe mitte gap10"><div class="spalte gap4 wachsen minw0"><span class="name24">${esc(r.name)}</span>
          <span class="sub13">${esc(r.unter || '')}${r.unter ? ' · ' : ''}${r.ebenen} Ebenen × ${r.plaetze} Plätze · ${sorten.size} Sorten</span></div>
          <button class="knopf rahmen klein" id="rg-bearb">Bearbeiten</button></div>
        <div class="regal-gitter" style="max-width:${Math.max(420, r.plaetze * 92)}px">${ebenen}</div>
        ${R.suche ? `<span class="hinweis">Gesucht: <b>${esc(sorteNach(R.suche)?.kurzname || '')}</b> – ${platzVon(R.suche).length ? 'orange markiert' : 'liegt noch auf keinem Platz: einfach auf einen freien Platz klicken oder aus der Ablage ziehen'}</span>` : ''}`;
    }
    this.ablage();
  },
  ablage() {
    const q = R.q.trim();
    const ohne = S.sorten.filter((s) => s.spulen > 0 && !platzVon(s.uid).length);
    const liste = q ? S.sorten.filter((s) => treffer(s, q)) : ohne;
    $('#rg-ablage-info').textContent = q ? `${liste.length} Treffer` : `${ohne.length} Sorten mit Bestand ohne Platz`;
    $('#rg-ablage').innerHTML = liste.slice(0, 200).map((s) => `<div class="zieh${R.suche === s.uid ? ' vorschlag' : ''}" draggable="true" data-zieh="${esc(s.uid)}" title="Auf einen Platz ziehen">
      <div class="swatch s22" style="background:${esc(s.hex)}"></div><div class="spalte minw0 wachsen"><span class="ellipse" style="font-weight:600;font-size:13px">${esc(s.kurzname)}</span>
      <span class="sub12 ellipse">${esc(s.hersteller)} · ${s.spulen} Sp.</span></div></div>`).join('') || '<div class="leer-hinweis">Alles einsortiert</div>';
  },
};
