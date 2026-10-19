// FilamentLager Waage – Web-Oberfläche für den Raspberry Pi (gleiche Logik wie ui.cpp der ESP32-Firmware)
"use strict";

const SPULE_DA_G = 20;
const MAX_ZEILEN = 80;
const FILTER = ["PLA", "PETG", "ABS·ASA", "Andere", "Alle"];

const S = {
  z: null,                // letzter Zustand vom Server
  sorten: [],
  lagerVersion: -1,
  seite: "wiegen",
  auswahlUid: "",         // per Hand gewählte Sorte für die Spule auf dem Teller
  spuleWarDa: false,
  auswahlModus: false,
  auswahlTag: "",
  detailUid: "",
  filter: 4,
  kalG: 1000,
  haupt: "nichts",
  aktiv: { sorte: null, tag: "", netto: 0 },
  karteSchluessel: "",
};

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const gramm = (g) => `${g} g`;
const spulenText = (n) => `${n} ${n === 1 ? "Spule" : "Spulen"}`;
const komma = (v, n) => Number(v).toFixed(n).replace(".", ",");

async function post(pfad, daten = {}) {
  try {
    await fetch(pfad, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(daten) });
  } catch (e) {
    meldung("Dienst nicht erreichbar", true);
  }
}

// ------------------------------------------------------------ Skalierung (Test im Browser)
function skalieren() {
  const f = Math.min(innerWidth / 800, innerHeight / 480);
  const app = $("app");
  app.style.transform = f > 1.01 || f < 0.99 ? `scale(${f})` : "";
  app.style.left = f !== 1 ? `${(innerWidth - 800 * f) / 2}px` : "0";
}
addEventListener("resize", skalieren);
addEventListener("mousemove", () => document.body.classList.add("maus"), { once: true });

// ------------------------------------------------------------ Meldung
let toastTimer = 0;
function meldung(text, fehler = false) {
  const t = $("toast");
  t.textContent = text;
  t.classList.toggle("fehler", fehler);
  t.classList.remove("versteckt");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add("versteckt"), 2800);
}

// ------------------------------------------------------------ Hilfen zur Sorte
function kurzname(s) { return s.kurzname || s.summary; }
function unterzeile(s) { return [s.hersteller, ...s.besonderheiten.slice(0, 2)].filter(Boolean).join(" · "); }
function restText(s) {
  if (s.spulen === 0) return "aufgebraucht";
  if (!s.reste.length) return s.refills ? `${s.refills} Refill` : "voll";
  if (s.reste.length === 1) return `Rest ${gramm(s.reste[0])}`;
  return `${s.reste.length} Reste`;
}
function passtFilter(s, f) {
  const m = (s.material || "").toLowerCase();
  const pla = m.startsWith("pla") || m.startsWith("apla");
  const petg = m.includes("pet") && !pla;
  const abs = m.startsWith("abs") || m.startsWith("asa") || m.startsWith("pcabs");
  return [pla, petg, abs, !pla && !petg && !abs, true][f];
}
const findeNfc = (tag) => (tag ? S.sorten.find((s) => s.nfc.includes(tag)) : undefined);
const findeUid = (uid) => S.sorten.find((s) => s.uid === uid);

// ------------------------------------------------------------ Seiten
function zeige(seite) {
  S.seite = seite;
  for (const n of ["wiegen", "lager", "detail", "einst"]) $(`seite-${n}`).classList.toggle("versteckt", n !== seite);
  const nav = seite === "detail" ? "lager" : seite;
  document.querySelectorAll(".nav-knopf").forEach((b) => b.classList.toggle("an", b.dataset.seite === nav));
  tastatur(false);
  if (seite === "wiegen") { $("kopf-sub").textContent = "Waage"; S.karteSchluessel = ""; wiegen(); }
  if (seite === "lager") { kopfLager(); liste(); reste(); }
  if (seite === "detail") { $("kopf-sub").textContent = "Sorte im Detail"; detail(); }
  if (seite === "einst") { $("kopf-sub").textContent = "Einstellungen"; einst(); }
}
function kopfLager() {
  $("kopf-sub").textContent = S.auswahlModus ? "Sorte für die Spule wählen" : `${S.sorten.length} Sorten`;
}

// ------------------------------------------------------------ Kopfzeile
function kopf() {
  const z = S.z;
  const aus = z.sensor_ok ? "" : "aus";
  $("dot-l").className = "dot " + (z.tag_l ? "an" : aus);
  $("dot-r").className = "dot " + (z.tag_r ? "an" : aus);
  $("dot-ha").className = "dot " + (z.ha_ok ? "an" : "aus");
  $("wlan").classList.toggle("aus", !z.ip);
  $("uhr").textContent = z.uhrzeit || "--:--";
}

// ------------------------------------------------------------ Wiegen
function karteChips(chips) {
  $("k-chips").innerHTML = chips.map(([t, a]) => `<span class="chip ${a}">${esc(t)}</span>`).join("");
}

function wiegen() {
  const z = S.z;
  if (!z) return;
  const tag = z.tag_r || z.tag_l;
  const leser = z.tag_r ? "rechts" : "links";
  const da = z.brutto_g >= SPULE_DA_G;
  if (S.spuleWarDa && !da) S.auswahlUid = "";
  S.spuleWarDa = da;
  let sorte = findeNfc(tag);
  if (!sorte && S.auswahlUid) sorte = findeUid(S.auswahlUid);
  const leer = sorte && sorte.leer_g > 0 ? sorte.leer_g : z.leer_standard_g;
  const brutto = Math.round(z.brutto_g);
  const netto = Math.max(0, brutto - leer);
  const soll = sorte ? sorte.gewicht : 1000;
  const proz = da ? Math.min(100, Math.floor((netto * 100) / Math.max(1, soll))) : 0;
  S.aktiv = { sorte, tag, netto };

  const farbe = proz < 15 && da ? "var(--rot)" : "var(--gruen)";
  $("ring").style.background = `conic-gradient(${farbe} 0 ${proz}%, var(--rand2) ${proz}% 100%)`;
  $("spule").style.background = sorte ? sorte.hex : "var(--rand)";
  $("zahl").textContent = da ? netto : 0;
  $("proz").textContent = !z.sensor_ok ? "Sensor antwortet nicht"
    : !da ? "Spule auflegen" : `${proz} % von ${gramm(soll)}${z.stabil ? "" : " · misst …"}`;
  $("chip-brutto").textContent = `Brutto ${gramm(brutto)}`;
  $("chip-leer").textContent = `Leerspule ${gramm(leer)}`;

  const schluessel = [sorte ? sorte.uid + JSON.stringify(sorte) : "", tag, da].join("|");
  if (schluessel === S.karteSchluessel) return;
  S.karteSchluessel = schluessel;

  const haupt = $("btn-haupt");
  if (sorte) {
    $("k-titel").textContent = tag ? `Erkannt · NFC ${leser}` : "Gewählte Sorte";
    $("k-swatch").style.background = sorte.hex;
    $("k-name").textContent = kurzname(sorte);
    $("k-sub").textContent = sorte.hersteller;
    const c = sorte.besonderheiten.slice(0, 2).map((b) => [b, ""]);
    c.push([`${spulenText(sorte.spulen)} im Lager`, "g"]);
    if (sorte.reste.length) c.push([`Rest bisher ${gramm(sorte.reste[sorte.reste.length - 1])}`, "o"]);
    karteChips(c);
    S.haupt = da ? "speichern" : "nichts";
    haupt.textContent = "✓  Rest speichern";
    $("btn-wahl").textContent = "Andere Sorte";
  } else if (tag) {
    $("k-titel").textContent = `Neuer Tag · NFC ${leser}`;
    $("k-swatch").style.background = "var(--rand)";
    $("k-name").textContent = "Neue Spule";
    $("k-sub").textContent = `UID ${tag}`;
    karteChips([["Keiner Sorte zugeordnet", "o"]]);
    S.haupt = "zuordnen";
    haupt.textContent = "Sorte zuordnen";
    $("btn-wahl").textContent = "Sorte wählen";
  } else {
    $("k-titel").textContent = "Keine Spule erkannt";
    $("k-swatch").style.background = "var(--rand2)";
    $("k-name").textContent = da ? "Ohne NFC-Tag" : "Teller ist leer";
    $("k-sub").textContent = da ? "Sorte aus dem Lager wählen" : "Spule mit Tag auflegen";
    karteChips([]);
    S.haupt = da ? "waehlen" : "nichts";
    haupt.textContent = da ? "Sorte wählen" : "✓  Rest speichern";
    $("btn-wahl").textContent = "Sorte wählen";
  }
  haupt.disabled = S.haupt === "nichts";
}

function auswahlStarten() {
  S.auswahlModus = true;
  S.auswahlTag = S.aktiv.tag;
  zeige("lager");
}

$("btn-haupt").onclick = () => {
  if (S.haupt === "speichern" && S.aktiv.sorte) {
    post("/api/rest", { sorte: S.aktiv.sorte.uid, netto: S.aktiv.netto, tag: S.aktiv.tag });
  } else if (S.haupt === "zuordnen" || S.haupt === "waehlen") {
    auswahlStarten();
  }
};
$("btn-tara").onclick = () => post("/api/tara");
$("btn-wahl").onclick = auswahlStarten;

// ------------------------------------------------------------ Lager
function filterLeiste() {
  $("filter").innerHTML = FILTER.map((f, i) =>
    `<button class="filter${i === S.filter ? " an" : ""}" data-f="${i}">${f}</button>`).join("");
}
$("filter").onclick = (e) => {
  const b = e.target.closest("button");
  if (!b) return;
  S.filter = Number(b.dataset.f);
  liste();
};

function liste() {
  filterLeiste();
  const q = $("suche").value.trim().toLowerCase();
  let n = 0, weitere = 0;
  const html = [];
  for (const s of S.sorten) {
    if (!passtFilter(s, S.filter)) continue;
    if (q && !`${s.summary} ${s.hersteller} ${s.farbe}`.toLowerCase().includes(q)) continue;
    if (n >= MAX_ZEILEN) { weitere++; continue; }
    n++;
    html.push(`<div class="zeile" data-uid="${esc(s.uid)}">
      <div class="swatch s30" style="background:${esc(s.hex)}"></div>
      <div class="spalte gap2 wachsen minw0"><span class="ellipse" style="font-weight:600;font-size:15px">${esc(kurzname(s))}</span>
        <span class="sub14 ellipse">${esc(unterzeile(s))}</span></div>
      ${s.nfc.length ? '<span class="chip b">NFC</span>' : ""}
      <div class="rechts"><div class="anz${s.spulen ? "" : " leer"}">${spulenText(s.spulen)}</div>
        <div class="sub14">${esc(restText(s))}</div></div></div>`);
  }
  if (!n) html.push(`<div class="leer-hinweis">${S.sorten.length ? "Keine passende Sorte" : "Lager wird geladen …"}</div>`);
  if (weitere) html.push(`<div class="leer-hinweis">… und ${weitere} weitere – Suche verwenden</div>`);
  $("liste").innerHTML = html.join("");
}

$("liste").onclick = (e) => {
  const z = e.target.closest(".zeile");
  if (!z) return;
  const s = findeUid(z.dataset.uid);
  if (!s) return;
  if (S.auswahlModus) {
    S.auswahlModus = false;
    S.auswahlUid = s.uid;
    if (S.auswahlTag && !s.nfc.includes(S.auswahlTag)) post("/api/zuordnen", { sorte: s.uid, tag: S.auswahlTag });
    zeige("wiegen");
  } else {
    S.detailUid = s.uid;
    zeige("detail");
  }
};

function reste() {
  const alle = [];
  for (const s of S.sorten) for (const g of s.reste) alle.push([s, g]);
  alle.sort((a, b) => b[1] - a[1]);
  $("reste").innerHTML = alle.length ? alle.slice(0, 7).map(([s, g]) => `<div class="rest-zeile">
      <div class="swatch s22" style="background:${esc(s.hex)}"></div>
      <div class="wachsen minw0"><div class="ellipse" style="font-size:13px;font-weight:600">${esc(kurzname(s))}</div>
        <div class="balken"><div style="width:${Math.max(2, (g * 100) / s.gewicht)}%;background:${g < 250 ? "var(--rot)" : "var(--orange)"}"></div></div></div>
      <span style="font-size:13px;font-weight:700;width:50px;text-align:right">${gramm(g)}</span></div>`).join("")
    : '<span class="sub14">Keine angebrochenen Rollen</span>';
}

// ------------------------------------------------------------ Bildschirmtastatur für die Suche
const TASTEN = ["1234567890", "QWERTZUIOPÜ", "ASDFGHJKLÖÄ", "YXCVBNM-"];
function tastatur(an) {
  const t = $("tastatur");
  if (an && !t.childElementCount) {
    t.innerHTML = TASTEN.map((r) => `<div class="t-reihe">${[...r].map((k) => `<button data-k="${k}">${k}</button>`).join("")}</div>`).join("")
      + `<div class="t-reihe"><button class="breit" data-k="⌫">⌫ Löschen</button><button class="breit" data-k=" ">Leerzeichen</button>
         <button class="breit" data-k="leeren">Leeren</button><button class="breit ok" data-k="ok">Fertig</button></div>`;
  }
  t.classList.toggle("versteckt", !an);
}
$("suche").onfocus = () => tastatur(true);
$("tastatur").onclick = (e) => {
  const b = e.target.closest("button");
  if (!b) return;
  const i = $("suche");
  const k = b.dataset.k;
  if (k === "ok") { tastatur(false); i.blur(); return; }
  if (k === "⌫") i.value = i.value.slice(0, -1);
  else if (k === "leeren") i.value = "";
  else i.value += k.toLowerCase();
  liste();
};

// ------------------------------------------------------------ Detail
function bereich(a, b, e) {
  if (a < 0 && b < 0) return "–";
  if (a < 0 || a === b) return `${b}${e}`;
  if (b < 0) return `${a}${e}`;
  return `${a}–${b}${e}`;
}
function detail() {
  const s = findeUid(S.detailUid);
  const ziel = $("seite-detail");
  if (!s) { ziel.innerHTML = ""; return; }
  const chips = [s.material ? `<span class="chip">${esc(s.material)}</span>` : "",
    ...s.besonderheiten.slice(0, 2).map((b) => `<span class="chip b">${esc(b)}</span>`)].join("");
  let trocknen = s.trocknen_c > 0 ? `${s.trocknen_c} °C` : "–";
  if (s.trocknen_c > 0 && s.trocknen_h > 0) trocknen += ` · ${s.trocknen_h} h`;
  const zeile = (n, w) => `<div class="info-zeile"><span>${n}</span><span>${esc(w || "–")}</span></div>`;
  ziel.innerHTML = `
    <section class="panel gruen links300">
      <div class="reihe gap10 mitte"><button class="knopf rahmen zurueck" id="d-zurueck" aria-label="Zurück zum Lager">‹</button>
        <div class="spalte minw0"><span class="name20 ellipse">${esc(kurzname(s))}</span><span class="sub14">${esc(s.hersteller)}</span></div></div>
      <div class="reihe gap12 mitte"><div class="swatch s64" style="background:${esc(s.hex)}"></div>
        <div class="spalte gap6"><div class="chips">${chips}</div><span class="sub14">${komma(s.durchmesser, 2)} mm · ${gramm(s.gewicht)}</span></div></div>
      <div class="reihe gap10"><div class="kachel"><b>${s.spulen}</b><span>Spulen</span></div>
        <div class="kachel"><b>${s.reste.length}</b><span>angebrochen</span></div></div>
      <div class="reihe gap8">
        <button class="knopf rahmen quadrat" data-d="-1" aria-label="Eine Spule weniger">−</button>
        <button class="knopf primaer wachsen" id="d-wiegen" style="height:52px;font-size:15px">Spule wiegen</button>
        <button class="knopf rahmen quadrat" data-d="1" aria-label="Eine Spule mehr">+</button></div>
    </section>
    <section class="spalte gap10 wachsen minw0">
      <div class="reihe gap10"><div class="wert-kachel"><span class="ueber">Düse</span><b>${bereich(s.duese_min, s.duese_max, " °C")}</b></div>
        <div class="wert-kachel"><span class="ueber">Trocknen</span><b>${trocknen}</b></div></div>
      <div class="info-liste">${zeile("Reste", s.reste.map(gramm).join(", "))}${zeile("Bambu-ID", s.bambu_id)}${zeile("Farbcode", s.fcode)}
        ${zeile("EAN", s.codes[0])}${zeile("NFC-Tags", s.nfc.length ? `${s.nfc.length} zugeordnet` : "")}</div>
    </section>`;
  $("d-zurueck").onclick = () => zeige("lager");
  $("d-wiegen").onclick = () => { S.auswahlUid = s.uid; zeige("wiegen"); };
  ziel.querySelectorAll("[data-d]").forEach((b) => (b.onclick = () => post("/api/spulen", { sorte: s.uid, delta: Number(b.dataset.d) })));
}

// ------------------------------------------------------------ Einstellungen
function einst() {
  const z = S.z || {};
  $("e-info").textContent = [
    `WLAN: ${z.ip || "getrennt"}`,
    `Home Assistant: ${z.ha_ok ? "verbunden" : "keine Verbindung"}`,
    `Sensor: ${z.sensor_ok ? "verbunden" : "antwortet nicht"}`,
    `Sorten im Lager: ${S.sorten.length}`,
    `Leerspule Standard: ${gramm(z.leer_standard_g ?? 250)}`,
    `Version: ${z.version || ""}`,
  ].join("\n");
}
document.querySelectorAll("[data-kal]").forEach((b) => (b.onclick = () => {
  S.kalG = Math.max(100, Math.min(5000, S.kalG + Number(b.dataset.kal)));
  $("kal-wert").textContent = gramm(S.kalG);
}));
$("e-tara").onclick = () => post("/api/tara");
$("e-kal").onclick = () => post("/api/kalibrieren", { g: S.kalG });
$("e-laden").onclick = () => post("/api/neu_laden");

document.querySelectorAll(".nav-knopf").forEach((b) => (b.onclick = () => { S.auswahlModus = false; zeige(b.dataset.seite); }));

// ------------------------------------------------------------ Daten vom Dienst
async function lagerLaden() {
  const r = await fetch("/api/lager");
  const d = await r.json();
  S.sorten = d.sorten;
  S.lagerVersion = d.version;
  S.karteSchluessel = "";
  if (S.seite === "lager") { kopfLager(); liste(); reste(); }
  else if (S.seite === "detail") detail();
}

function zustand(z) {
  S.z = z;
  kopf();
  if (z.lager_version !== S.lagerVersion) {
    S.lagerVersion = z.lager_version;
    lagerLaden();
  }
  if (S.seite === "wiegen") wiegen();
  else if (S.seite === "einst") einst();
}

function verbinden() {
  const es = new EventSource("/api/ereignisse");
  es.addEventListener("zustand", (e) => zustand(JSON.parse(e.data)));
  es.addEventListener("meldung", (e) => { const m = JSON.parse(e.data); meldung(m.text, m.fehler); });
  es.onerror = () => { es.close(); setTimeout(verbinden, 2000); };
}

skalieren();
filterLeiste();
zeige("wiegen");
verbinden();
