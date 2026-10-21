'use strict';
/* Einstellungen: Home Assistant, Waage, Etikettendrucker, Demo */

const WAAGE_ARTEN = [['pi', 'Über Pad 7 / Pi'], ['tcp', 'Sensorplatine direkt'], ['demo', 'Demo'], ['aus', 'Keine']];

SEITEN.einst = {
  titel: 'Einstellungen',
  zeichnen(el) {
    const e = S.einst;
    $('#titel-sub').textContent = `Version ${S.version}`;
    el.innerHTML = `<div class="einst">
      <section class="panel"><h2><span class="dot ${S.demo ? 'warn' : S.ha.verbunden ? 'an' : 'aus'}"></span>Home Assistant</h2>
        <div class="feld"><label>Adresse</label><input id="h-url" value="${esc(e.ha.url)}" placeholder="http://homeassistant.local:8123"></div>
        <div class="feld"><label>Langlebiges Zugriffstoken</label><input id="h-token" type="password" placeholder="${e.ha.tokenGesetzt ? '•••••••• gespeichert – zum Ändern neu einfügen' : 'HA → Profil → Sicherheit → Langlebige Zugriffstokens'}" autocomplete="off"></div>
        <div class="raster2"><div class="feld"><label>Filamentlager (Todo-Liste)</label><input id="h-todo" value="${esc(e.ha.todo)}"></div>
          <div class="e-zeile" style="align-self:end;height:40px"><span class="wachsen">Zertifikat prüfen</span><button class="schalter${e.ha.zertifikatPruefen ? ' an' : ''}" id="h-zert" aria-label="Zertifikat prüfen"></button></div></div>
        <span class="hinweis">Von unterwegs geht auch deine Nabu-Casa-Adresse (https://…ui.nabu.casa). Das Token liegt verschlüsselt im Schlüsselbund des Mac.
          Die App liest <code>todo.filament</code> und schreibt nur die Felder, die auch die Waage schreibt (Spulen, Reste, NFC, Leerspule) – siehe docs/LAGER.md.</span>
        <div class="reihe gap8"><button class="knopf rahmen" id="h-test">Verbindung testen</button><button class="knopf primaer" id="h-ok">Speichern</button>
          ${e.ha.tokenGesetzt ? '<button class="knopf rahmen rot rechtsb" id="h-weg">Token löschen</button>' : ''}</div>
      </section>

      <section class="panel"><h2><span class="dot ${S.waage.verbunden ? 'an' : e.waage.art === 'aus' ? '' : 'aus'}"></span>Waage</h2>
        <div class="seg" id="w-art">${WAAGE_ARTEN.map(([id, n]) => `<button data-art="${id}" class="${e.waage.art === id ? 'an' : ''}">${n}</button>`).join('')}</div>
        <div class="raster2" id="w-adr"><div class="feld"><label>Adresse der Waage</label><input id="w-adresse" value="${esc(e.waage.adresse)}" placeholder="filamentwaage.local"></div>
          <div class="feld"><label>Port</label><input id="w-port" type="number" value="${esc(e.waage.port)}"></div></div>
        <span class="hinweis" id="w-hinweis"></span>
        <div class="raster2"><div class="feld"><label>Leere Spule wiegt (Standard)</label><input id="w-leer" type="number" min="0" max="1000" value="${esc(e.waage.leerspule_g)}"></div>
          <div class="feld"><label>Prüfgewicht zum Kalibrieren (g)</label><input id="w-kalg" type="number" value="1000"></div></div>
        <div class="reihe gap8"><button class="knopf primaer" id="w-ok">Speichern</button><button class="knopf rahmen" id="w-tara2">Tara</button><button class="knopf rahmen" id="w-kal">Kalibrieren</button></div>
      </section>

      <section class="panel"><h2>Etikettendrucker</h2>
        <span class="hinweis">Für den Bluetooth-Druck auf dem Phomemo M20Pro (300 dpi). Druckbreite = Punkte pro Zeile am Druckkopf; bleibt das Etikett leer oder verschoben, hier anpassen (typisch 576 oder 600).</span>
        <div class="raster2"><div class="feld"><label>Druckbreite (Punkte)</label><input id="p-breite" type="number" step="8" value="${esc(e.etikett.druckbreite_punkte)}"></div>
          <div class="feld"><label>Auflösung (dpi)</label><input id="p-dpi" type="number" value="${esc(e.etikett.dpi)}"></div>
          <div class="feld"><label>Dichte (1–15)</label><input id="p-dichte" type="number" min="1" max="15" value="${esc(e.etikett.dichte)}"></div></div>
        <div class="reihe gap8"><button class="knopf primaer" id="p-ok">Speichern</button></div>
      </section>

      <section class="panel"><h2>Demo und Info</h2>
        <div class="e-zeile"><span class="wachsen">Demo-Modus mit Beispieldaten<br><span class="sub12">Ohne Token läuft die App immer im Demo-Modus.</span></span><button class="schalter${e.demo ? ' an' : ''}" id="i-demo" aria-label="Demo-Modus"></button></div>
        <div class="info-liste">
          <div class="info-zeile"><span>Version</span><span>${esc(S.version)}</span></div>
          <div class="info-zeile"><span>Lager</span><span>${S.sorten.length} Sorten${S.demo ? ' (Beispiel)' : ''}</span></div>
          <div class="info-zeile"><span>Drucker</span><span>${S.drucker.length}</span></div>
        </div>
        <span class="hinweis">Teil des Projekts bambu-scale. Aufbau angelehnt an Tiger Studio Manager (TigerTag-Projekt, MIT) – eigene App, nicht mit TigerTag verbunden.</span>
      </section></div>`;

    const artHinweis = () => {
      const art = $('#w-art .an')?.dataset.art;
      $('#w-adr').classList.toggle('versteckt', art === 'demo' || art === 'aus');
      $('#w-hinweis').innerHTML = art === 'pi'
        ? 'Liest das Gewicht von der Oberfläche der Pad-7-/Pi-Waage (Port 8080) – die Waage bleibt am Pad 7 bedienbar. Dafür auf dem Pad in <code>/etc/filamentwaage/config.toml</code> unter <code>[server]</code> <code>adresse = "0.0.0.0"</code> setzen und den Dienst neu starten.'
        : art === 'tcp' ? 'Verbindet sich direkt mit der Sensorplatine (Port 4711). Die Platine bedient nur eine Gegenstelle – ein verbundenes Pad 7 wird dabei getrennt.'
          : art === 'demo' ? 'Beispielszenen ohne Hardware.' : 'Ohne Waage – Lager, Drucker und Etiketten gehen trotzdem.';
    };
    artHinweis();
    $('#w-art').addEventListener('click', (ev) => {
      const b = ev.target.closest('[data-art]'); if (!b) return;
      $$('#w-art button').forEach((x) => x.classList.toggle('an', x === b));
      if (b.dataset.art === 'pi' && Number($('#w-port').value) === 4711) $('#w-port').value = 8080;
      if (b.dataset.art === 'tcp' && Number($('#w-port').value) === 8080) $('#w-port').value = 4711;
      artHinweis();
    });
    $('#h-zert').onclick = (ev) => ev.currentTarget.classList.toggle('an');
    $('#i-demo').onclick = async (ev) => {
      ev.currentTarget.classList.toggle('an');
      await this.speichern({ demo: ev.currentTarget.classList.contains('an') }, 'Gespeichert');
    };
    const haDaten = () => ({ url: $('#h-url').value.trim().replace(/\/+$/, ''), todo: $('#h-todo').value.trim() || 'todo.filament', zertifikatPruefen: $('#h-zert').classList.contains('an') });
    $('#h-test').onclick = () => aktion(() => window.fl.ha_testen({ ...haDaten(), token: $('#h-token').value.trim() }));
    $('#h-ok').onclick = () => {
      const t = $('#h-token').value.trim();
      this.speichern({ ha: haDaten(), ...(t ? { token: t } : {}) }, 'Home Assistant gespeichert');
    };
    if ($('#h-weg')) $('#h-weg').onclick = async () => {
      if (await bestaetigen('Token löschen?', 'Ohne Token läuft die App im Demo-Modus.', 'Löschen', true)) this.speichern({ token: '' }, 'Token gelöscht');
    };
    $('#w-ok').onclick = () => this.speichern({ waage: { art: $('#w-art .an').dataset.art, adresse: $('#w-adresse').value.trim() || 'filamentwaage.local',
      port: Number($('#w-port').value) || 8080, leerspule_g: Math.max(0, Number($('#w-leer').value) || 250) } }, 'Waage gespeichert');
    $('#w-tara2').onclick = () => aktion(() => window.fl.waage_tara(), false);
    $('#w-kal').onclick = async () => {
      const g = Number($('#w-kalg').value) || 1000;
      if (await bestaetigen('Kalibrieren', `Teller leer? Dann zuerst <b>Tara</b>. Jetzt genau <b>${g} g</b> auflegen und bestätigen.`, 'Kalibrieren')) aktion(() => window.fl.waage_kal({ g }), `Kalibriere mit ${g} g …`);
    };
    $('#p-ok').onclick = () => this.speichern({ etikett: { ...e.etikett, druckbreite_punkte: Math.max(8, Number($('#p-breite').value) || 576),
      dpi: Math.max(100, Number($('#p-dpi').value) || 300), dichte: Math.max(1, Math.min(15, Number($('#p-dichte').value) || 8)) } }, 'Gespeichert');
  },
  async speichern(teil, text) {
    const e = await aktion(() => window.fl.einst_speichern(teil), text);
    if (!e) return;
    S.einst = e;
    const st = await window.fl.app_start();
    S.demo = st.demo;
    neuZeichnen();
    this.zeichnen($('#seite'));
  },
  aktualisieren() { /* Eingaben nicht überschreiben – Status steht in der Seitenleiste */ },
};
