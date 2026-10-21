'use strict';
/* Start: Daten holen, Live-Ereignisse verbinden, erste Seite zeigen */
(async function start() {
  const st = await window.fl.app_start();
  S.einst = st.einst; S.version = st.version; S.demo = st.demo; S.plattform = st.plattform;
  if (st.plattform === 'darwin') document.body.classList.add('mac');
  const [lager, drucker, waage] = await Promise.all([window.fl.lager_holen(), window.fl.drucker_holen(), window.fl.waage_holen()]);
  const lagerSetzen = (l) => { S.sorten = l.sorten; S.lagerOk = l.ok; S.lagerGrund = l.grund; S.lagerStand = l.stand; S.demo = l.demo; };
  lagerSetzen(lager);
  S.drucker = drucker.liste; S.ausgeblendet = drucker.ausgeblendet;
  S.waage = waage;

  window.fl.auf('lager', (l) => { lagerSetzen(l); neuZeichnen('lager'); });
  window.fl.auf('drucker', (d) => { S.drucker = d.liste; S.ausgeblendet = d.ausgeblendet; neuZeichnen('drucker'); });
  window.fl.auf('waage', (w) => {
    const vorher = S.waage;
    S.waage = w;
    if (vorher.verbunden !== w.verbunden || vorher.tagL !== w.tagL || vorher.tagR !== w.tagR) statusZeichnen();
    if (S.seite === 'wiegen') SEITEN.wiegen.aktualisieren('waage');
  });
  window.fl.auf('meldung', (m) => toast(m.text, m.fehler));
  window.fl.auf('ha-status', (h) => { S.ha = h; S.demo = h.demo; neuZeichnen('ha'); });
  window.fl.auf('bt-geraete', (l) => btListeZeigen(l));

  zeigen('wiegen');
  statusZeichnen();
})();
