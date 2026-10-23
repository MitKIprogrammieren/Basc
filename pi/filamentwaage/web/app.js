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
// Textgröße gilt nur für diesen Browser (am Mac klein, am Pad 7 normal): größere virtuelle Fläche, dann skaliert
const TEXTGROESSEN = [["gross", "Groß", 1.15], ["normal", "Normal", 1], ["klein", "Klein", 0.85], ["sehrklein", "Sehr klein", 0.72]];
function textGroesse() {
  try { return localStorage.getItem("fl_textgroesse") || "normal"; } catch (e) { return "normal"; }
}
function textFaktor() { return (TEXTGROESSEN.find((t) => t[0] === textGroesse()) || TEXTGROESSEN[1])[2]; }
function skalieren() {
  const t = textFaktor(), W = Math.round(800 / t), H = Math.round(480 / t);
  const f = Math.min(innerWidth / W, innerHeight / H);
  const app = $("app");
  app.style.width = `${W}px`;
  app.style.height = `${H}px`;
  app.style.transform = f > 1.01 || f < 0.99 ? `scale(${f})` : "";
  app.style.left = f > 1.01 || f < 0.99 ? `${(innerWidth - W * f) / 2}px` : "0";
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
  // Waage: grün = misst und ist kalibriert, rot = Wägezelle antwortet nicht, grau = nicht kalibriert
  $("dot-waage").className = "dot " + (!z.sensor_ok ? "aus" : z.kalibriert ? "an" : "");
  // Drucker (nur Sensorplatine): grün = eingerichtet, rot = letzter Druck fehlgeschlagen, grau = keiner
  $("st-drucker").classList.toggle("versteckt", z.drucker === undefined);
  if (z.drucker !== undefined) $("dot-drucker").className = "dot " + (z.drucker === 2 ? "aus" : z.drucker === 1 ? "an" : "");
  $("wlan").classList.toggle("aus", !z.ip);
  $("wlan").classList.toggle("an", !!z.ip);
  // Pi am Netzwerkkabel: dort steht LAN statt WLAN (die Sensorplatine meldet kein "netz")
  if ($("wlan-text")) $("wlan-text").textContent = z.netz === "lan" ? "LAN" : "WLAN";
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
  S.aktiv = { sorte, tag, netto, da };

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
    const ams = amsSlotFuerTag(tag);
    if (ams) c.push([`${ams.drucker.name} · ${ams.ams.kurz}${ams.slot.slot}${ams.slot.rest >= 0 ? ` · ${ams.slot.rest} %` : ""}`, "ams"]);
    karteChips(c);
    S.haupt = da ? "speichern" : "nichts";
    haupt.textContent = "✓  Rest speichern";
    $("btn-wahl").textContent = "Andere Sorte";
  } else if (tag) {
    $("k-titel").textContent = `Neuer Tag · NFC ${leser}`;
    $("k-swatch").style.background = "var(--rand)";
    $("k-name").textContent = "Neue Spule";
    $("k-sub").textContent = `UID ${tag}`;
    const ams = amsSlotFuerTag(tag);
    S.vorschlag = ams ? vorschlagFuerSlot(ams.slot) : null;
    karteChips(ams ? [[`${ams.drucker.name} · ${ams.ams.kurz}${ams.slot.slot}`, "ams"], [S.vorschlag ? `Vorschlag: ${kurzname(S.vorschlag)}` : ams.slot.name, "g"]]
      : [["Keiner Sorte zugeordnet", "o"]]);
    S.haupt = "zuordnen";
    haupt.textContent = S.vorschlag ? `Als ${kurzname(S.vorschlag)} speichern` : "Sorte zuordnen";
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
  $("btn-etikett").disabled = !sorte;
}

function auswahlStarten() {
  S.auswahlModus = true;
  if (!S.aktiv.tag) S.vorschlag = null;
  S.auswahlTag = S.aktiv.tag;
  zeige("lager");
}

$("btn-haupt").onclick = () => {
  if (S.haupt === "speichern" && S.aktiv.sorte) {
    post("/api/rest", { sorte: S.aktiv.sorte.uid, netto: S.aktiv.netto, tag: S.aktiv.tag });
  } else if (S.haupt === "zuordnen" && S.vorschlag) {
    S.auswahlUid = S.vorschlag.uid;
    post("/api/zuordnen", { sorte: S.vorschlag.uid, tag: S.aktiv.tag });
    S.karteSchluessel = "";
  } else if (S.haupt === "zuordnen" || S.haupt === "waehlen") {
    auswahlStarten();
  }
};
$("btn-tara").onclick = () => post("/api/tara");
$("btn-wahl").onclick = auswahlStarten;
$("btn-etikett").onclick = () => {
  const a = S.aktiv;
  if (a.sorte) etikettOeffnen(a.sorte, a.da && S.z && S.z.stabil ? a.netto : 0);
};

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
  const vorschlag = S.auswahlModus && S.vorschlag ? S.vorschlag : null;
  const reihe = vorschlag ? [vorschlag, ...S.sorten.filter((x) => x !== vorschlag)] : S.sorten;
  for (const s of reihe) {
    if (!passtFilter(s, S.filter) && s !== vorschlag) continue;
    if (q && !`${s.summary} ${s.hersteller} ${s.farbe}`.toLowerCase().includes(q)) continue;
    if (n >= MAX_ZEILEN) { weitere++; continue; }
    n++;
    html.push(`<div class="zeile${s === vorschlag ? " vorschlag" : ""}" data-uid="${esc(s.uid)}">
      <div class="swatch s30" style="background:${esc(s.hex)}"></div>
      <div class="spalte gap2 wachsen minw0"><span class="ellipse" style="font-weight:600;font-size:15px">${esc(kurzname(s))}</span>
        <span class="sub14 ellipse">${esc(unterzeile(s))}</span></div>
      ${s === vorschlag ? '<span class="chip ams">Vorschlag aus AMS</span>' : s.nfc.length ? '<span class="chip b">NFC</span>' : ""}
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

// ------------------------------------------------------------ Bildschirmtastatur (Suche und Eingabefelder)
const TASTEN = ["1234567890", "QWERTZUIOPÜ", "ASDFGHJKLÖÄ", "YXCVBNM-"];
const TASTEN_SONDER = ["1234567890", "qwertzuiop", "asdfghjkl.", "yxcvbnm:/_"];
let tastaturZiel = null;
let gross = false;
function tastaturZeichnen() {
  const reihen = tastaturZiel && tastaturZiel.dataset.tastatur === "frei" ? (gross ? TASTEN : TASTEN_SONDER) : TASTEN;
  $("tastatur").innerHTML = reihen.map((r) => `<div class="t-reihe">${[...r].map((k) => `<button data-k="${esc(k)}">${esc(k)}</button>`).join("")}</div>`).join("")
    + `<div class="t-reihe">${tastaturZiel && tastaturZiel.dataset.tastatur === "frei" ? '<button class="breit" data-k="⇧">⇧ Groß</button>' : ""}
       <button class="breit" data-k="⌫">⌫ Löschen</button><button class="breit" data-k=" ">Leerzeichen</button>
       <button class="breit" data-k="leeren">Leeren</button><button class="breit ok" data-k="ok">Fertig</button></div>`;
}
function tastatur(an, ziel) {
  if (an) { tastaturZiel = ziel || $("suche"); gross = false; tastaturZeichnen(); }
  $("tastatur").classList.toggle("versteckt", !an);
  $("e-inhalt").style.paddingBottom = an && tastaturZiel && tastaturZiel.id !== "suche" ? "220px" : "";
  if (!an && tastaturZiel) { tastaturZiel.blur(); tastaturZiel = null; }
}
document.addEventListener("focusin", (e) => {
  const el = e.target;
  if (el.matches && (el.id === "suche" || el.matches("input[data-tastatur]"))) {
    tastatur(true, el);
    if (el.id !== "suche") setTimeout(() => el.scrollIntoView({ block: "start", behavior: "smooth" }), 0);
  }
});
$("tastatur").onclick = (e) => {
  const b = e.target.closest("button");
  if (!b || !tastaturZiel) return;
  const i = tastaturZiel;
  const k = b.dataset.k;
  if (k === "ok") { tastatur(false); return; }
  if (k === "⇧") { gross = !gross; tastaturZeichnen(); return; }
  if (k === "⌫") i.value = i.value.slice(0, -1);
  else if (k === "leeren") i.value = "";
  else i.value += i.id === "suche" ? k.toLowerCase() : k;
  i.dispatchEvent(new Event("input", { bubbles: true }));
};
$("suche").addEventListener("input", () => liste());

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
  const tw = Etikett.trocknen(s);
  let trocknen = tw ? `${tw.richtwert ? "~" : ""}${tw.c} °C${tw.h > 0 ? ` · ${tw.h} h` : ""}` : "–";
  const getr = Etikett.datum(s.getrocknet);
  const heute = new Date();
  const heuteIso = `${heute.getFullYear()}-${String(heute.getMonth() + 1).padStart(2, "0")}-${String(heute.getDate()).padStart(2, "0")}`;
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
        <div class="wert-kachel"><span class="ueber">Trocknen${tw && tw.richtwert ? " (Richtwert)" : ""}</span><b>${trocknen}</b></div></div>

      <div class="info-liste">
        <div class="info-zeile mitte"><span>Zuletzt getrocknet</span><span class="reihe gap8 mitte">${getr || "–"}
          <button class="mini-knopf" id="d-getrocknet"${s.getrocknet === heuteIso ? " disabled" : ""}>Heute</button></span></div>
        <div class="info-zeile mitte"><span>Lagerort</span>${S.ortBearbeiten === s.uid
          ? `<span class="reihe gap6 mitte wachsen" style="justify-content:flex-end"><input id="d-ort" class="et-eigene klein" data-tastatur="frei" maxlength="40" placeholder="z. B. Regal 2 oben" value="${esc(s.ort || "")}" aria-label="Lagerort"><button class="mini-knopf" id="d-ort-ok">OK</button></span>`
          : `<span class="reihe gap8 mitte">${esc(s.ort || "–")}<button class="mini-knopf" id="d-ort-an">Ändern</button></span>`}</div>
        ${zeile("Reste", s.reste.map(gramm).join(", "))}${zeile("Bambu-ID", s.bambu_id)}${zeile("Farbcode", s.fcode)}
        ${zeile("EAN", s.codes[0])}${zeile("NFC-Tags", s.nfc.length ? `${s.nfc.length} zugeordnet` : "")}</div>
      <button class="knopf rahmen h50" id="d-etikett">${icon("etikett")}Etikett drucken</button>
    </section>`;
  $("d-zurueck").onclick = () => zeige("lager");
  $("d-getrocknet").onclick = () => post("/api/feld", { sorte: s.uid, feld: "getrocknet", wert: heuteIso });
  if ($("d-ort-an")) $("d-ort-an").onclick = () => { S.ortBearbeiten = s.uid; detail(); $("d-ort").focus(); };
  if ($("d-ort-ok")) {
    $("d-ort-ok").onclick = () => {
      S.ortBearbeiten = null;
      tastatur(false);
      post("/api/feld", { sorte: s.uid, feld: "ort", wert: $("d-ort").value.trim() });
      detail();
    };
    $("d-ort").onkeydown = (e) => { if (e.key === "Enter") $("d-ort-ok").click(); };
  }
  $("d-wiegen").onclick = () => { S.auswahlUid = s.uid; zeige("wiegen"); };
  $("d-etikett").onclick = () => {
    const a = S.aktiv;
    etikettOeffnen(s, a.sorte && a.sorte.uid === s.uid && a.da && S.z && S.z.stabil ? a.netto : 0);
  };
  ziel.querySelectorAll("[data-d]").forEach((b) => (b.onclick = () => post("/api/spulen", { sorte: s.uid, delta: Number(b.dataset.d) })));
}

// ------------------------------------------------------------ Einstellungen
const AKZENTE = ["#4caf50", "#26c6da", "#2196f3", "#3f51b5", "#9c27b0", "#e91e63", "#f44336", "#ff9800", "#fbc02d", "#8bc34a"];
const AKZENT_NAMEN = ["Grün", "Türkis", "Blau", "Indigo", "Lila", "Pink", "Rot", "Orange", "Gelb", "Hellgrün"];
const ICONS = {
  design: '<path d="M12 3a9 9 0 1 0 0 18c1 0 1.5-.8 1.5-1.6 0-1-.8-1.4-.8-2.4 0-.9.7-1.5 1.6-1.5H17a4 4 0 0 0 4-4c0-4.7-4-8.5-9-8.5Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10.5" cy="7" r="1"/><circle cx="15" cy="7.5" r="1"/>',
  wlan: '<path d="M2 8.8a15 15 0 0 1 20 0"/><path d="M5.5 12.5a10 10 0 0 1 13 0"/><path d="M9 16.2a5 5 0 0 1 6 0"/><circle cx="12" cy="19.5" r=".8"/>',
  ha: '<path d="M3 11 12 4l9 7v9H3z"/><path d="M9 20v-6h6v6"/>',
  bambu: '<rect x="4" y="4" width="16" height="16" rx="2.5"/><path d="M4 8.5h16"/><path d="M10.2 8.5v2.2h3.6V8.5"/><path d="M7.5 16.5h9"/>',
  waage: '<path d="M4 20h16"/><path d="M6 20l2-9h8l2 9"/><path d="M12 11V5"/><path d="M8 5h8"/>',
  etikett: '<path d="M7 9V3h10v6"/><rect x="3" y="9" width="18" height="8" rx="2"/><path d="M7 14h10v7H7z"/>',
  system: '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M9 9h6v6H9z"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M1 9h3M1 15h3M20 9h3M20 15h3"/>',
};
const MENUE = [
  ["design", "Design"], ["wlan", "WLAN"], ["ha", "Home Assistant"], ["bambu", "Bambu Lab"], ["waage", "Waage"], ["etikett", "Etiketten"], ["system", "System"],
];
S.e = null;          // Einstellungen vom Dienst
S.eSeite = "design";
S.ams = { drucker: [], abbuchungen: [] };
S.amsDrucker = "";
S.netze = null;
S.wlanWahl = "";
S.resetFrage = false;

const icon = (n) => `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[n]}</svg>`;
const schalter = (an, key) => `<button class="schalter${an ? " an" : ""}" data-schalter="${key}" role="switch" aria-checked="${!!an}" aria-label="${key}"></button>`;
const seg = (key, wert, optionen) => `<div class="seg">${optionen.map(([v, t]) =>
  `<button class="${String(v) === String(wert) ? "an" : ""}" data-seg="${key}" data-v="${v}">${t}</button>`).join("")}</div>`;
const pill = (ok, t) => `<span class="status-pill ${ok === null ? "grau" : ok ? "ok" : "aus"}">${esc(t)}</span>`;

async function einstLaden() {
  try {
    S.e = await (await fetch("/api/einstellungen")).json();
    designAnwenden();
    if (S.seite === "einst") einst();
  } catch (e) { /* Dienst noch nicht bereit */ }
}
async function einstSetzen(teil) {
  Object.assign(S.e, teil);
  designAnwenden();
  try {
    const r = await fetch("/api/einstellungen", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(teil) });
    S.e = await r.json();
  } catch (e) { meldung("Speichern fehlgeschlagen", true); }
  designAnwenden();
  einst();
}

function mischen(hex, ziel, anteil) {
  const a = hex.match(/\w\w/g).map((x) => parseInt(x, 16)), b = ziel.match(/\w\w/g).map((x) => parseInt(x, 16));
  return "#" + a.map((v, i) => Math.round(v + (b[i] - v) * anteil).toString(16).padStart(2, "0")).join("");
}
function designAnwenden() {
  if (!S.e) return;
  const app = $("app"), f = S.e.akzent || "#4caf50";
  app.style.setProperty("--gruen", f);
  app.style.setProperty("--gruen-t", mischen(f, "#ffffff", 0.5));
  app.style.setProperty("--nav-akt", mischen(f, "#0d0f12", 0.84));
  app.style.setProperty("--chip-g", mischen(f, "#15181c", 0.82));
  app.style.filter = S.e.helligkeit < 100 ? `brightness(${0.35 + 0.65 * S.e.helligkeit / 100})` : "";
  $("liste").classList.toggle("kompakt-liste", !!S.e.kompakt);
}

function menueText(n) {
  const e = S.e || {}, z = S.z || {};
  if (n === "design") return `${AKZENT_NAMEN[AKZENTE.indexOf(e.akzent)] || "Eigene Farbe"} · ${e.helligkeit ?? 100} %`;
  if (n === "wlan") return e.wlan?.ssid ? `${e.wlan.ssid}` : "nicht verbunden";
  if (n === "ha") return z.ha_ok ? `verbunden · ${e.ha?.todo || ""}` : "keine Verbindung";
  if (n === "bambu") { const n2 = (S.ams.drucker || []).filter((d) => (e.drucker || []).includes(d.id)).length; return `${n2} Drucker über Home Assistant`; }
  if (n === "etikett") {
    if (!S.et) return "wird geladen …";
    const f = (Etikett.FORMATE[S.et.format] || {}).name || "";
    return `${S.et.adresse ? S.et.name || S.et.adresse : "kein Drucker"} · ${f}`;
  }
  if (n === "waage") return `Leerspule ${e.leerspule_g ?? 250} g · NFC Stufe ${e.nfc_stufe ?? 3}`;
  return `Version ${z.version || ""}`;
}
function einstMenue() {
  $("e-menue").innerHTML = MENUE.map(([n, t]) => `<button class="m-zeile${S.eSeite === n ? " an" : ""}" data-m="${n}">
    <span class="m-icon">${icon(n)}</span><span class="m-text"><b>${t}</b><small>${esc(menueText(n))}</small></span>
    <span class="pfeil" aria-hidden="true">›</span></button>`).join("");
}
$("e-menue").onclick = (e) => {
  const b = e.target.closest(".m-zeile");
  if (!b) return;
  S.eSeite = b.dataset.m;
  S.resetFrage = false;
  tastatur(false);
  if (S.eSeite === "wlan" && !S.netze) wlanSuchen();
  if (S.eSeite === "bambu") amsLaden();
  if (S.eSeite === "etikett") etLaden();
  einst();
};

function einst() {
  einstMenue();
  const e = S.e;
  const z = S.z || {};
  const ziel = $("e-inhalt");
  if (S.eSeite === "etikett") { ziel.innerHTML = etEinstHtml(); return; }
  if (!e) { ziel.innerHTML = '<span class="sub14">Einstellungen werden geladen …</span>'; return; }
  let h = "";
  if (S.eSeite === "design") {
    h = `<span class="e-titel">Design</span>
      <div class="e-block"><span class="ueber">Akzentfarbe</span>
        <div class="farben">${AKZENTE.map((f) => `<button class="farbe${f === e.akzent ? " an" : ""}" data-akzent="${f}" style="background:${f}" aria-label="Farbe ${f}"></button>`).join("")}</div></div>
      <div class="e-block"><span class="ueber">Helligkeit</span>
        <div class="e-zeile"><input type="range" class="regler" id="e-hell" min="10" max="100" step="5" value="${e.helligkeit}" aria-label="Helligkeit"><span class="e-wert" id="e-hell-w">${e.helligkeit} %</span></div></div>
      <div class="e-block"><span class="ueber">Display ausschalten</span>${seg("display_aus_min", e.display_aus_min, [[0, "Nie"], [5, "5 min"], [15, "15 min"], [30, "30 min"]])}</div>
      <div class="e-block"><span class="ueber gross-ss">TEXTGRÖẞE</span><div class="seg">${TEXTGROESSEN.map(([k, t]) =>
        `<button class="${k === textGroesse() ? "an" : ""}" data-text="${k}">${t}</button>`).join("")}</div>
        <span class="hinweis">Kleiner zeigt mehr auf einmal – praktisch im Browser am Mac. Gilt nur für dieses Gerät.</span></div>
      <div class="e-block"><span class="ueber">Lagerliste</span>${seg("kompakt", e.kompakt, [[false, "Normal"], [true, "Kompakt"]])}</div>`;
  } else if (S.eSeite === "wlan") {
    const w = e.wlan || {};
    h = `<div class="e-zeile"><span class="e-titel wachsen">WLAN</span>${pill(!!w.ssid, w.ssid ? "verbunden" : "getrennt")}</div>
      ${w.ssid ? `<div class="info-liste" style="flex:0 0 auto"><div class="info-zeile"><span>Netzwerk</span><span>${esc(w.ssid)}</span></div>
        <div class="info-zeile"><span>IP-Adresse</span><span>${esc(w.ip || "–")}</span></div>
        <div class="info-zeile"><span>Signal</span><span>${w.signal ?? "–"} %</span></div></div>` : ""}
      <div class="e-zeile"><span class="ueber wachsen">Netzwerke in der Nähe</span><button class="knopf rahmen" style="height:38px;padding:0 12px;font-size:13px" id="e-scan">Neu suchen</button></div>
      <div>${S.netze === null ? '<span class="sub14">Suche …</span>' : S.netze.map((n) => `<div class="netz" data-ssid="${esc(n.ssid)}">
        <span class="balken-wlan" aria-label="Signal ${n.signal} %">${[25, 50, 75, 90].map((g, i) => `<i class="${n.signal >= g ? "an" : ""}" style="height:${4 + i * 3}px"></i>`).join("")}</span>
        <span class="wachsen">${esc(n.ssid)}</span>${n.ssid === w.ssid ? pill(true, "aktiv") : n.gesichert ? '<span class="sub14">🔒</span>' : ""}</div>
        ${S.wlanWahl === n.ssid ? `<div class="e-zeile" style="padding:8px 0">
          ${n.gesichert ? `<div class="feld wachsen"><input id="e-wlan-pw" type="password" data-tastatur="frei" inputmode="none" autocomplete="off" placeholder="Passwort" aria-label="WLAN-Passwort"></div>` : '<span class="sub14 wachsen">Offenes Netzwerk</span>'}
          <button class="knopf primaer" style="height:42px;padding:0 16px;font-size:14px" id="e-wlan-ok">Verbinden</button></div>` : ""}`).join("")}</div>`;
  } else if (S.eSeite === "ha") {
    const ha = e.ha || {};
    h = `<div class="e-zeile"><span class="e-titel wachsen">Home Assistant</span>${pill(z.ha_ok, z.ha_ok ? "verbunden" : "keine Verbindung")}</div>
      <div class="feld"><label for="e-ha-url">Adresse</label><input id="e-ha-url" data-tastatur="frei" inputmode="none" autocomplete="off" value="${esc(ha.url || "")}"></div>
      <div class="feld"><label for="e-ha-todo">Filamentliste</label><input id="e-ha-todo" data-tastatur="frei" inputmode="none" autocomplete="off" value="${esc(ha.todo || "todo.filament")}"></div>
      <div class="feld"><label for="e-ha-token">Zugriffstoken</label><input id="e-ha-token" data-tastatur="frei" inputmode="none" autocomplete="off" placeholder="${ha.token_gesetzt ? `gespeichert · endet auf …${esc(ha.token_ende || "")}` : "noch kein Token"}"></div>
      <div class="e-zeile">
        <button class="knopf rahmen h50 wachsen" id="e-ha-test">Verbindung testen</button>
        <button class="knopf primaer h50 wachsen" id="e-ha-ok" style="font-size:15px">Speichern</button></div>
      <button class="knopf rahmen h50" id="e-qr">Token am Handy eingeben</button>
      <span class="hinweis">Ein Token ist über 180 Zeichen lang. Am bequemsten fügst du es am Handy ein: Code scannen, Token einfügen, fertig.</span>`;
  } else if (S.eSeite === "bambu") {
    const dr = S.ams.drucker || [];
    if (!S.amsDrucker && dr.length) S.amsDrucker = (dr.find((d) => (e.drucker || []).includes(d.id)) || dr[0]).id;
    const d = dr.find((x) => x.id === S.amsDrucker);
    h = `<span class="e-titel">Bambu Lab</span>
      <span class="hinweis">Die Drucker kommen über deine Bambu-Lab-Integration in Home Assistant. Hier wählst du, welche die Waage kennen soll.</span>
      <div class="e-block"><span class="ueber">Drucker</span>
        ${dr.length ? dr.map((x) => `<div class="e-zeile"><span class="e-name">${esc(x.name)}</span>${pill(x.online, x.online ? "online" : "aus")}${schalter((e.drucker || []).includes(x.id), "drucker:" + x.id)}</div>`).join("")
          : '<span class="sub14">Keine Drucker gefunden</span>'}</div>
      <div class="e-block"><div class="e-zeile"><span class="e-name"><b>Spule im AMS erkennen</b><br><span class="sub14">Zeigt beim Wiegen, in welchem Slot eine Bambu-Spule zuletzt steckte, und schlägt bei neuen Tags die Sorte vor.</span></span>${schalter(e.ams_suche, "ams_suche")}</div></div>
      ${d ? `<div class="e-block"><span class="ueber">AMS-Belegung</span>
        <div class="tabs">${dr.filter((x) => (e.drucker || []).includes(x.id)).map((x) => `<button class="${x.id === S.amsDrucker ? "an" : ""}" data-drucker="${x.id}">${esc(x.name)}</button>`).join("")}</div>
        ${(d.ams || []).map((a) => `<div class="e-zeile"><span class="sub14 wachsen">${esc(a.name)}</span>${a.feuchte != null ? `<span class="sub14">${a.feuchte} % Feuchte</span>` : ""}</div>
          <div class="ams">${a.slots.map((sl) => `<div class="slot${sl.leer ? " leer" : ""}"><div class="spule-klein" style="background:${sl.leer ? "transparent" : esc(sl.hex)}"></div>
            <b>${esc(a.kurz)}${sl.slot}${sl.aktiv ? " · aktiv" : ""}</b><small>${sl.leer ? "leer" : esc(sl.name)}</small>${sl.leer ? "" : `<small>${sl.rest >= 0 ? sl.rest + " % laut AMS" : ""}</small>`}</div>`).join("")}</div>`).join("") || '<span class="sub14">Kein AMS</span>'}</div>` : ""}
      <div class="e-block"><span class="ueber">Abbuchen nach dem Druck</span>
        <div class="e-zeile"><span class="e-name">Verbrauch wird nach jedem Druck automatisch abgezogen</span>${pill(e.abbuchung_aktiv, e.abbuchung_aktiv ? "aktiv" : "aus")}</div>
        <span class="hinweis">Das erledigt „Ernsthaft gelagert“ in Home Assistant. Die Waage zeigt die letzten Abbuchungen und korrigiert Reste, wenn du eine Spule nachwiegst.</span>
        ${(S.ams.abbuchungen || []).slice(0, 4).map((x) => `<div class="e-zeile"><span class="sub14" style="width:86px">${esc(x.zeit)}</span><span class="e-name">${esc(x.text)}</span></div>`).join("")}</div>`;
  } else if (S.eSeite === "waage") {
    h = `<span class="e-titel">Waage</span>
      <div class="e-block"><span class="ueber">Tara und Kalibrieren</span>
        <span class="hinweis">Teller leer machen und Tara drücken. Dann das Kalibriergewicht auflegen und Kalibrieren drücken.</span>
        <div class="e-zeile"><button class="knopf rahmen h50 wachsen" id="e-tara">Tara</button>
          <button class="knopf rahmen quadrat" data-kal="-100" aria-label="100 g weniger">−</button>
          <span class="name20" style="width:86px;text-align:center" id="kal-wert">${gramm(S.kalG)}</span>
          <button class="knopf rahmen quadrat" data-kal="100" aria-label="100 g mehr">+</button></div>
        <button class="knopf primaer h54" id="e-kal">Kalibrieren</button></div>
      <div class="e-block"><span class="ueber">Leere Spule</span>
        <div class="e-zeile"><span class="e-name">Gewicht, wenn die Sorte keinen eigenen Wert hat</span>
          <button class="knopf rahmen quadrat" data-leer="-5" aria-label="5 g weniger">−</button><span class="e-wert" style="width:64px;text-align:center">${gramm(e.leerspule_g)}</span>
          <button class="knopf rahmen quadrat" data-leer="5" aria-label="5 g mehr">+</button></div></div>
      <div class="e-block"><span class="ueber">Empfindlichkeit der NFC-Leser</span>${seg("nfc_stufe", e.nfc_stufe, [[1, "1"], [2, "2"], [3, "3"], [4, "4"], [5, "5"]])}
        <span class="hinweis">Höher liest Tags aus größerem Abstand. Liest ein Leser den Tag des anderen, eine Stufe herunter.</span></div>`;
  } else {
    h = `<span class="e-titel">System</span>
      <div class="info-liste" style="flex:0 0 auto">
        <div class="info-zeile"><span>Version</span><span>${esc(z.version || "")}</span></div>
        <div class="info-zeile"><span>Sensor</span><span>${z.sensor_ok ? "verbunden" : "antwortet nicht"}</span></div>
        <div class="info-zeile"><span>Sorten im Lager</span><span>${S.sorten.length}</span></div>
        <div class="info-zeile"><span>IP-Adresse</span><span>${esc(z.ip || "–")}</span></div></div>
      ${e.update ? `<button class="knopf primaer h50" data-system="einspielen">Update einspielen …</button>
        <input type="file" id="e-update" hidden>
        <span class="hinweis">Datei FilamentLager-Webflasher.html wählen – die Waage nimmt sich Firmware und Oberfläche daraus und startet neu. Kalibrierung, WLAN und Einstellungen bleiben.</span>
        ${S.update ? `<div class="balken"><div style="width:${S.update.p}%;background:var(--gruen)"></div></div><span class="sub14">${esc(S.update.text)}</span>` : ""}` : ""}
      <div class="e-zeile"><button class="knopf rahmen h50 wachsen" data-system="neu_laden">Lager neu laden</button>
        ${e.update ? "" : '<button class="knopf rahmen h50 wachsen" data-system="update">Nach Update suchen</button>'}</div>
      <button class="knopf rahmen h50" data-system="neustart">Neu starten</button>
      <button class="knopf rahmen rot h50" data-system="werk">${S.resetFrage ? "Wirklich alles zurücksetzen? Nochmal tippen" : "Werkseinstellungen"}</button>`;
  }
  ziel.innerHTML = h;
}

$("e-inhalt").addEventListener("input", (e) => {
  if (e.target.id === "e-hell") {
    $("e-hell-w").textContent = `${e.target.value} %`;
    S.e.helligkeit = Number(e.target.value);
    designAnwenden();
  }
});
$("e-inhalt").addEventListener("change", (e) => {
  if (e.target.dataset.hersteller !== undefined) return bildHochladen(e.target);
  if (e.target.id === "e-update" && e.target.files[0]) return updateEinspielen(e.target.files[0]);
  if (e.target.id === "e-hell") einstSetzen({ helligkeit: Number(e.target.value) });
});
$("e-inhalt").onclick = async (e) => {
  const b = e.target.closest("button, .netz");
  if (!b) return;
  const d = b.dataset;
  if (d.et) return etEinstKlick(d);
  if (d.akzent) return einstSetzen({ akzent: d.akzent });
  if (d.text) { try { localStorage.setItem("fl_textgroesse", d.text); } catch (e) { /* ohne Speicher nur bis zum Neuladen */ } skalieren(); return einst(); }
  if (d.seg) { let v = d.v; v = v === "true" ? true : v === "false" ? false : isNaN(Number(v)) ? v : Number(v); return einstSetzen({ [d.seg]: v }); }
  if (d.schalter) {
    if (d.schalter.startsWith("drucker:")) {
      const id = d.schalter.slice(8), liste = new Set(S.e.drucker || []);
      liste.has(id) ? liste.delete(id) : liste.add(id);
      return einstSetzen({ drucker: [...liste] });
    }
    return einstSetzen({ [d.schalter]: !S.e[d.schalter] });
  }
  if (d.drucker) { S.amsDrucker = d.drucker; return einst(); }
  if (d.kal) { S.kalG = Math.max(100, Math.min(5000, S.kalG + Number(d.kal))); $("kal-wert").textContent = gramm(S.kalG); return; }
  if (d.leer) return einstSetzen({ leerspule_g: Math.max(0, Math.min(1000, S.e.leerspule_g + Number(d.leer))) });
  if (d.ssid !== undefined && b.classList.contains("netz")) { S.wlanWahl = S.wlanWahl === d.ssid ? "" : d.ssid; einst(); $("e-wlan-pw")?.focus(); return; }
  if (d.system) {
    if (d.system === "werk" && !S.resetFrage) { S.resetFrage = true; return einst(); }
    S.resetFrage = false;
    if (d.system === "neu_laden") return post("/api/neu_laden");
    if (d.system === "einspielen") return $("e-update").click();
    post("/api/system", { aktion: d.system });
    return einst();
  }
  switch (b.id) {
    case "e-tara": return post("/api/tara");
    case "e-kal": return post("/api/kalibrieren", { g: S.kalG });
    case "e-scan": return wlanSuchen();
    case "e-wlan-ok": {
      tastatur(false);
      const pw = $("e-wlan-pw") ? $("e-wlan-pw").value : "";
      meldung(`Verbinde mit ${S.wlanWahl} …`);
      const r = await (await fetch("/api/wlan", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ ssid: S.wlanWahl, passwort: pw }) })).json();
      meldung(r.text, !r.ok);
      if (r.ok) { S.wlanWahl = ""; await einstLaden(); }
      return;
    }
    case "e-ha-test":
    case "e-ha-ok": {
      tastatur(false);
      const daten = { url: $("e-ha-url").value.trim(), todo: $("e-ha-todo").value.trim() };
      if ($("e-ha-token").value.trim()) daten.token = $("e-ha-token").value.trim();
      const r = await (await fetch(b.id === "e-ha-test" ? "/api/ha/test" : "/api/ha", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(daten) })).json();
      meldung(r.text, !r.ok);
      if (r.ok && b.id === "e-ha-ok") await einstLaden();
      return;
    }
    case "e-qr": return qrZeigen();
  }
};

async function wlanSuchen() {
  S.netze = null;
  if (S.seite === "einst" && S.eSeite === "wlan") einst();
  try {
    // Die Sensorplatine sucht im Hintergrund und meldet {laeuft: true}, bis die Liste fertig ist
    for (let i = 0; i < 20; i++) {
      const r = await (await fetch("/api/wlan/netze")).json();
      if (Array.isArray(r)) { S.netze = r; break; }
      await new Promise((ok) => setTimeout(ok, 700));
    }
    if (!S.netze) S.netze = [];
  } catch (e) { S.netze = []; }
  if (S.seite === "einst" && S.eSeite === "wlan") einst();
}

function qrZeigen() {
  const url = (S.e && S.e.einrichten_url) || `http://${(S.z && S.z.ip) || "waage.local"}:8080/einrichten`;
  $("qr-url").textContent = url;
  $("qr-code").innerHTML = "";
  if (window.QRCode) new QRCode($("qr-code"), { text: url, width: 168, height: 168, correctLevel: QRCode.CorrectLevel.M });
  $("qr").classList.remove("versteckt");
}
$("qr-zu").onclick = () => $("qr").classList.add("versteckt");

// ------------------------------------------------------------ AMS (über Home Assistant)
async function amsLaden() {
  try {
    S.ams = await (await fetch("/api/ams")).json();
    S.karteSchluessel = "";
    if (S.seite === "einst" && S.eSeite === "bambu") einst();
  } catch (e) { /* ohne AMS weiter */ }
}
function amsSlotFuerTag(tag) {
  if (!tag || !S.e || !S.e.ams_suche) return null;
  const t = tag.toUpperCase();
  for (const d of S.ams.drucker || []) {
    if (!(S.e.drucker || []).includes(d.id)) continue;
    for (const a of d.ams || []) for (const sl of a.slots) {
      if (sl.tag && sl.tag.toUpperCase().startsWith(t.slice(0, 8))) return { drucker: d, ams: a, slot: sl };
    }
  }
  return null;
}
function farbAbstand(a, b) {
  const x = a.match(/\w\w/g).map((v) => parseInt(v, 16)), y = b.match(/\w\w/g).map((v) => parseInt(v, 16));
  return x.reduce((s, v, i) => s + (v - y[i]) ** 2, 0);
}
function vorschlagFuerSlot(sl) {
  if (!sl || !sl.filament_id) return null;
  const kand = S.sorten.filter((s) => s.bambu_id === sl.filament_id);
  if (!kand.length) return null;
  kand.sort((a, b) => farbAbstand(a.hex, sl.hex.slice(0, 7)) - farbAbstand(b.hex, sl.hex.slice(0, 7)));
  return farbAbstand(kand[0].hex, sl.hex.slice(0, 7)) < 19200 ? kand[0] : null;
}

document.querySelectorAll(".nav-knopf").forEach((b) => (b.onclick = () => { S.auswahlModus = false; zeige(b.dataset.seite); }));

// ------------------------------------------------------------ Etiketten (Phomemo)
S.et = null;          // Etiketten-Einstellungen vom Dienst
S.etDialog = null;    // { sorte, rest, format }
S.etGeraete = null;   // gefundene Bluetooth-Geräte
S.etSucht = false;

async function etLaden() {
  try {
    S.et = await (await fetch("/api/etikett/einstellungen")).json();
    if (S.seite === "einst") einst();
  } catch (e) { /* Dienst noch nicht bereit */ }
}
async function etSetzen(teil) {
  try {
    const r = await fetch("/api/etikett/einstellungen", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(teil) });
    S.et = await r.json();
  } catch (e) { meldung("Speichern fehlgeschlagen", true); }
  if (S.seite === "einst") einst();
}

// Etikett-Optionen: welche Angaben, eigene Zeile, hochkant – je Browser gemerkt
function etOptionen() {
  let o = null;
  try { o = JSON.parse(localStorage.getItem("fl_etikett") || "null"); } catch (e) { /* ohne Speicher */ }
  o = o && typeof o === "object" ? o : {};
  if (!Array.isArray(o.felder)) o.felder = Etikett.STANDARD_FELDER.slice();
  if (o.v !== 2) { o.felder = o.felder.filter((k) => k !== "rest"); o.v = 2; }  // Rest ist seit 2.8.2 aus
  o.eigene = typeof o.eigene === "string" ? o.eigene : "";
  o.hochkant = !!o.hochkant;
  o.formate = Array.isArray(o.formate) ? o.formate.filter((k) => Etikett.formatVon(k)) : [];
  o.sortierung = ["klein", "gross"].includes(o.sortierung) ? o.sortierung : "standard";
  return o;
}
function etOptionenSpeichern(o) {
  try { localStorage.setItem("fl_etikett", JSON.stringify(o)); } catch (e) { /* nur bis zum Neuladen */ }
}

function etikettOeffnen(sorte, rest) {
  const opt = etOptionen();
  const format = Etikett.formatVon(opt.format) ? opt.format : (S.et && S.et.format) || "expanded";
  S.etDialog = { sorte, rest: rest || 0, format, opt, eigenOffen: false };
  $("etikett").classList.remove("versteckt");
  etikettZeichnen();
}
function etOptionenHtml(d) {
  const f = Etikett.formatVon(d.format) || Etikett.FORMATE.expanded;
  const o = d.opt;
  const quer = f.b > f.h;
  const aus = quer ? `<div class="spalte gap6"><span class="ueber">Ausrichtung</span>${seg("et-hoch", o.hochkant ? 1 : 0, [[0, "Quer"], [1, "Hochkant"]])}</div>` : "";
  const wo = { rest: d.rest, eigene: o.eigene, leerStandard: S.z ? S.z.leer_standard_g : 0 };
  const leer = [];
  const chips = Etikett.FELDER.map(([k, n]) => {
    const an = o.felder.includes(k);
    const ohne = an && k !== "eigene" && !Etikett.hatWert(d.sorte, k, wo);
    if (ohne) leer.push(n);
    return `<button class="et-feld${an ? " an" : ""}${ohne ? " ohne" : ""}" data-feld="${k}" aria-pressed="${an}"${ohne ? ' title="Bei dieser Sorte nicht eingetragen"' : ""}>${esc(n)}${ohne ? " –" : ""}</button>`;
  }).join("");
  const hinweis = leer.length ? `<span class="et-ohne">Bei dieser Sorte nicht eingetragen: ${esc(leer.join(", "))} – diese Zeilen bleiben leer.</span>` : "";
  const eigene = o.felder.includes("eigene")
    ? `<input id="et-eigene" class="et-eigene" data-tastatur="frei" maxlength="40" placeholder="Eigener Text, z. B. Für Halloween-Deko" value="${esc(o.eigene)}">` : "";
  return `${aus}<div class="spalte gap6"><span class="ueber">Angaben auf dem Etikett</span><div class="et-felder">${chips}</div>${hinweis}${eigene}<span id="et-weg" class="sub14 et-weg"></span></div>`;
}
async function etikettZeichnen(nurVorschau) {
  const d = S.etDialog;
  if (!d) return;
  const f = Etikett.formatVon(d.format) || Etikett.FORMATE.expanded;
  if (!nurVorschau) {
    $("et-titel").textContent = d.sorte === Etikett.TEST ? "Testdruck" : kurzname(d.sorte);
    const hoch = d.opt.hochkant && f.b > f.h;
    $("et-info").textContent = [`${f.b} × ${f.h} mm${hoch ? " · hochkant" : ""}`, d.rest ? `Rest ${gramm(d.rest)}` : "",
      !S.et ? "" : S.et.bruecke ? "über Druckbrücke" : S.et.verbindung === "datei" ? "Testmodus ohne Drucker" : S.et.adresse ? S.et.name || S.et.adresse : "kein Drucker eingestellt"].filter(Boolean).join(" · ");
    const liste = [...Object.keys(Etikett.FORMATE), ...d.opt.formate].map((k) => [k, Etikett.formatVon(k)]);
    if (d.opt.sortierung !== "standard") {
      const r = d.opt.sortierung === "klein" ? 1 : -1;
      liste.sort(([, a], [, b]) => r * (a.b * a.h - b.b * b.h || a.b - b.b));
    }
    const pfeil = (auf) => `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${auf ? '<path d="M7 20V4M3 8l4-4 4 4M14 8h7M14 13h5M14 18h3"/>' : '<path d="M7 4v16M3 16l4 4 4-4M14 6h3M14 11h5M14 16h7"/>'}</svg>`;
    $("et-sort").innerHTML = [["standard", "Standard"], ["klein", `${pfeil(true)}Klein`], ["gross", `${pfeil(false)}Groß`]].map(([k, t]) =>
      `<button class="${d.opt.sortierung === k ? "an" : ""}" data-sort="${k}" aria-pressed="${d.opt.sortierung === k}">${t}</button>`).join("");
    $("et-formate").innerHTML = liste.map(([k, v]) =>
      `<button class="et-format${k === d.format ? " an" : ""}${v.eigen ? " eigen" : ""}" data-format="${k}"><b>${v.name}</b><small>${v.b} × ${v.h} mm</small>${v.eigen ? `<span class="et-x" data-weg="${k}" aria-label="Größe löschen">×</span>` : ""}</button>`).join("")
      + `<button class="et-format neu" data-eigen="1">+ Eigene Maße</button>`;
    $("et-eigen-form").innerHTML = d.eigenOffen ? `<div class="et-eigen-box spalte gap6">
        <span class="ueber gross-ss">EIGENE MAẞE</span>
        <div class="reihe gap6 mitte"><input id="et-mb" class="et-eigene klein mass" data-tastatur="frei" inputmode="decimal" value="${f.eigen ? f.b : 40}" aria-label="Breite in mm">
          <span>×</span><input id="et-mh" class="et-eigene klein mass" data-tastatur="frei" inputmode="decimal" value="${f.eigen ? f.h : 30}" aria-label="Höhe in mm"><span>mm</span>
          <button class="mini-knopf" id="et-mok" style="margin-left:auto">Übernehmen</button></div>
        <span id="et-mfehler" class="sub14" style="font-size:12px">Breite 15–${Etikett.KOPF_MM} mm (Druckkopf), Höhe 10–100 mm</span></div>` : "";
    if (d.eigenOffen) $("et-mok").onclick = () => {
      const b = Number(String($("et-mb").value).replace(",", ".")), h = Number(String($("et-mh").value).replace(",", "."));
      const k = `eigen-${Math.round(b)}x${Math.round(h)}`;
      if (!Etikett.formatVon(k)) {
        $("et-mfehler").textContent = `Geht nicht: Breite 15–${Etikett.KOPF_MM} mm, Höhe 10–100 mm`;
        $("et-mfehler").style.color = "var(--rot, #f44336)";
        return;
      }
      if (!d.opt.formate.includes(k)) d.opt.formate.push(k);
      d.format = d.opt.format = k;
      d.eigenOffen = false;
      tastatur(false);
      etOptionenSpeichern(d.opt);
      etikettZeichnen();
    };
    $("et-optionen").innerHTML = etOptionenHtml(d);
    const ein = $("et-eigene");
    if (ein) ein.oninput = () => { d.opt.eigene = ein.value; etOptionenSpeichern(d.opt); etikettZeichnen(true); };
  }
  const erg = await Etikett.zeichnen($("et-canvas"), d.sorte, {
    format: d.format, rest: d.rest, qr: S.et ? S.et.lager_url : "",
    bildVersion: S.et && S.et.bilder ? S.et.bilder[d.sorte.hersteller] || 0 : 0,
    felder: d.opt.felder, eigene: d.opt.eigene, hochkant: d.opt.hochkant,
    leerStandard: S.z ? S.z.leer_standard_g : 0,
  });
  const weg = $("et-weg");
  if (weg) weg.textContent = erg && erg.weg && erg.weg.length ? `Passt nicht mehr drauf: ${erg.weg.join(", ")}` : "";
}
$("et-formate").onclick = (e) => {
  const d = S.etDialog;
  if (!d) return;
  const weg = e.target.closest("[data-weg]");
  if (weg) {
    d.opt.formate = d.opt.formate.filter((k) => k !== weg.dataset.weg);
    if (d.format === weg.dataset.weg) d.format = (S.et && S.et.format) || "expanded";
    d.opt.format = d.format;
    etOptionenSpeichern(d.opt);
    return etikettZeichnen();
  }
  if (e.target.closest("[data-eigen]")) { d.eigenOffen = !d.eigenOffen; return etikettZeichnen(); }
  const b = e.target.closest("[data-format]");
  if (!b) return;
  d.format = d.opt.format = b.dataset.format;
  etOptionenSpeichern(d.opt);
  etikettZeichnen();
};
$("et-sort").onclick = (e) => {
  const d = S.etDialog;
  const b = e.target.closest("[data-sort]");
  if (!d || !b) return;
  d.opt.sortierung = b.dataset.sort;
  etOptionenSpeichern(d.opt);
  etikettZeichnen();
};
$("et-optionen").onclick = (e) => {
  const d = S.etDialog;
  if (!d) return;
  const f = e.target.closest("[data-feld]");
  const h = e.target.closest('[data-seg="et-hoch"]');
  if (f) {
    const k = f.dataset.feld;
    const an = new Set(d.opt.felder);
    an.has(k) ? an.delete(k) : an.add(k);
    d.opt.felder = Etikett.FELDER.map(([x]) => x).filter((x) => an.has(x));
  } else if (h) d.opt.hochkant = h.dataset.v === "1";
  else return;
  etOptionenSpeichern(d.opt);
  etikettZeichnen();
};
$("et-zu").onclick = () => { $("etikett").classList.add("versteckt"); S.etDialog = null; };
$("et-druck").onclick = async () => {
  if (!S.etDialog) return;
  const r = Etikett.raster($("et-canvas"));
  const so = S.etDialog.sorte;
  r.titel = so === Etikett.TEST ? "Testdruck" : [kurzname(so), so.hersteller && `(${so.hersteller})`].filter(Boolean).join(" ");
  try {
    const a = await fetch("/api/etikett/drucken", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(r) });
    const j = await a.json();
    if (!j.ok) meldung(j.text || "Druck fehlgeschlagen", true);
  } catch (e) { meldung("Dienst nicht erreichbar", true); }
};

function etEinstHtml() {
  const et = S.et;
  if (!et) return '<span class="sub14">Etiketten werden geladen …</span>';
  const verb = { ble: "Bluetooth LE", rfcomm: "Bluetooth (klassisch)", datei: "Datei (ohne Drucker)" }[et.verbindung] || et.verbindung;
  const geraete = S.etGeraete === null ? "" : S.etGeraete.length ? S.etGeraete.map((g) => `<div class="netz" data-et="waehlen" data-adresse="${esc(g.adresse)}" data-name="${esc(g.name)}">
      <span class="wachsen">${esc(g.name)}<br><span class="sub14">${esc(g.adresse)}</span></span>
      ${g.adresse === et.adresse ? pill(true, "gewählt") : g.phomemo ? pill(null, "Phomemo?") : ""}</div>`).join("")
    : '<span class="sub14">Kein Gerät gefunden – Drucker einschalten und nochmal suchen.</span>';
  const her = et.hersteller || [];
  return `<div class="e-zeile"><span class="e-titel wachsen">Etiketten</span>${et.verbunden ? pill(true, "mit der Waage verbunden") : pill(et.bruecke || et.adresse || et.verbindung === "datei" ? true : null, et.bruecke ? "über Druckbrücke" : et.adresse || et.verbindung === "datei" ? "eingerichtet" : "kein Drucker")}</div>
    <div class="e-block"><span class="ueber">Drucker</span>
      <div class="info-liste" style="flex:0 0 auto">
        <div class="info-zeile"><span>Drucker</span><span>${esc(et.name || (et.adresse ? "Phomemo" : "–"))}</span></div>
        <div class="info-zeile"><span>Adresse</span><span>${esc(et.adresse || "–")}</span></div>
        <div class="info-zeile"><span>Verbindung</span><span>${esc(verb)}</span></div></div>
      <div class="e-zeile"><button class="knopf rahmen h50 wachsen" data-et="suchen">${S.etSucht ? "Suche …" : "Drucker suchen"}</button>
        ${et.verbunden !== undefined ? `<button class="knopf rahmen rot h50 wachsen" data-et="trennen" aria-label="Verbindung zum Drucker trennen"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M7 12l5 5-1.5 1.5a3.5 3.5 0 0 1-5-5z"/><path d="M17 12l-5-5 1.5-1.5a3.5 3.5 0 0 1 5 5z"/><path d="M3 21l2.5-2.5M18.5 5.5L21 3M10 11l-2 2M13 14l-2 2"/></svg>Trennen</button>` : ""}
        <button class="knopf primaer h50 wachsen" data-et="test" style="font-size:15px">Testdruck</button></div>
      <div>${geraete}</div></div>
    ${et.bruecke !== undefined ? `<div class="e-block"><span class="ueber">Druckbrücke</span>
      <span class="hinweis">Für Drucker, die nur über klassisches Bluetooth drucken (Phomemo M20 Pro): Adresse des Druckdienstes auf dem Raspberry Pi. Leer lassen, wenn die Waage direkt druckt.</span>
      <div class="e-zeile"><div class="feld wachsen"><input id="et-bruecke" data-tastatur="frei" inputmode="none" autocomplete="off" value="${esc(et.bruecke)}" placeholder="http://192.168.1.250:8765" aria-label="Adresse der Druckbrücke"></div>
        <button class="knopf primaer" style="height:42px;padding:0 16px;font-size:14px" data-et="bruecke">Speichern</button></div></div>` : ""}
    <div class="e-block"><span class="ueber">Standardformat</span>
      <div class="seg">${Object.entries(Etikett.FORMATE).map(([k, v]) => `<button class="${k === et.format ? "an" : ""}" data-et="format" data-v="${k}">${v.name}</button>`).join("")}</div></div>
    <div class="e-block"><span class="ueber">Schwärzung</span>
      <div class="seg">${[[6, "Hell"], [10, "Normal"], [14, "Dunkel"]].map(([v, t]) => `<button class="${v === et.dichte ? "an" : ""}" data-et="dichte" data-v="${v}">${t}</button>`).join("")}</div></div>
    <div class="e-block"><span class="ueber">Herstellerbilder</span>
      ${et.bild_quelle === undefined
        ? '<span class="hinweis">Statt des Namens druckt das Etikett oben ein Bild, das du pro Hersteller hinterlegst (PNG, JPG oder WebP). Am bequemsten am Handy oder Mac: Oberfläche im Heimnetz öffnen und hier das Bild wählen – oder im Repository unter pi/hersteller ablegen.</span>'
        : `<span class="hinweis">Die Waage lädt ein Logo erst, wenn ein Etikett es braucht. Dateiname wie der Hersteller: Bambu Lab → bambu-lab.png. Leer lassen für den Ordner www/hersteller in Home Assistant.</span>
      <div class="feld"><label for="et-quelle">Bildquelle</label><div class="e-zeile"><input id="et-quelle" class="wachsen" data-tastatur="frei" inputmode="none" autocomplete="off" value="${esc(et.bild_quelle)}" placeholder="${esc(et.bild_quelle_standard || "")}">
        <button class="knopf primaer" style="height:42px;padding:0 16px;font-size:14px" data-et="quelle">Speichern</button></div></div>`}
      <div>${her.length ? her.map((h) => {
        const v = (et.bilder || {})[h];
        const st = (et.bild_status || {})[h];
        return `<div class="hersteller-zeile"><div class="h-bild">${v ? `<img src="/api/hersteller_bild?name=${encodeURIComponent(h)}&v=${v}" alt="">` : "<span>nur Name</span>"}</div>
          <span class="e-name ellipse">${esc(h)}</span>
          ${st ? `<span class="sub14">${esc(st)}</span>` : ""}
          ${et.bild_hochladen === false ? "" : `<label class="knopf rahmen klein">${v ? "Ändern" : "Bild wählen"}<input type="file" accept="image/png,image/jpeg,image/webp" data-hersteller="${esc(h)}" aria-label="Bild für ${esc(h)}"></label>`}
          ${(et.bilder_eigen || []).includes(h) ? `<button class="knopf rahmen rot klein" data-et="bild_weg" data-name="${esc(h)}">Entfernen</button>` : ""}</div>`;
      }).join("") : '<span class="sub14">Noch keine Hersteller im Lager</span>'}</div></div>`;
}

async function etEinstKlick(d) {
  if (d.et === "format") return etSetzen({ format: d.v });
  if (d.et === "quelle") { tastatur(false); await etSetzen({ bild_quelle: $("et-quelle").value.trim() }); return meldung("Bildquelle gespeichert"); }
  if (d.et === "dichte") return etSetzen({ dichte: Number(d.v) });
  if (d.et === "waehlen") { await etSetzen({ adresse: d.adresse, name: d.name }); return meldung(`${d.name} gewählt`); }
  if (d.et === "test") return etikettOeffnen(Etikett.TEST, 0);
  if (d.et === "bruecke") { tastatur(false); await etSetzen({ bruecke: $("et-bruecke").value.trim() }); return meldung($("et-bruecke").value.trim() ? "Druckbrücke gespeichert" : "Druckbrücke entfernt"); }
  if (d.et === "trennen") {
    try {
      const j = await (await fetch("/api/etikett/trennen", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" })).json();
      meldung(j.text || "Getrennt", !j.ok);
    } catch (e) { meldung("Waage nicht erreichbar", true); }
    S.etGeraete = null;
    return etLaden();
  }
  if (d.et === "bild_weg") {
    await post("/api/hersteller_bild/loeschen", { name: d.name });
    return etLaden();
  }
  if (d.et === "suchen" && !S.etSucht) {
    S.etSucht = true;
    einst();
    try {
      // Die Sensorplatine sucht im Hintergrund und meldet {laeuft: true}, bis die Liste fertig ist
      let r = { laeuft: true };
      for (let i = 0; i < 25 && r.laeuft; i++) {
        if (i) await new Promise((ok) => setTimeout(ok, 700));
        r = await (await fetch("/api/etikett/suchen")).json();
      }
      S.etGeraete = r.geraete || [];
      if (!r.ok) meldung(r.text, true);
    } catch (e) { S.etGeraete = []; meldung("Suche fehlgeschlagen", true); }
    S.etSucht = false;
    if (S.seite === "einst") einst();
  }
}

async function bildHochladen(input) {
  const datei = input.files && input.files[0];
  if (!datei) return;
  const name = input.dataset.hersteller;
  try {
    const r = await fetch(`/api/hersteller_bild?name=${encodeURIComponent(name)}`, { method: "POST", headers: { "Content-Type": datei.type || "application/octet-stream" }, body: datei });
    const j = await r.json();
    meldung(j.text, !j.ok);
  } catch (e) { meldung("Hochladen fehlgeschlagen", true); }
  etLaden();
}

// ------------------------------------------------------------ Update über WLAN (Sensorplatine)
// Nimmt Firmware und Oberfläche aus der Webflasher-Datei und lädt sie auf die Waage – kein USB nötig.
S.update = null;
function updateStand(p, text) { S.update = { p, text }; if (S.seite === "einst" && S.eSeite === "system") einst(); }
function hochladen(ziel, teil, von, bis) {
  return new Promise((ok, fehl) => {
    const bin = atob(teil.b64), u = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    const fd = new FormData();
    fd.append("datei", new Blob([u], { type: "application/octet-stream" }), teil.name);
    const x = new XMLHttpRequest();
    x.open("POST", `/api/update?ziel=${ziel}`);
    x.upload.onprogress = (e) => {
      if (e.lengthComputable) updateStand(Math.round(von + (bis - von) * e.loaded / e.total),
        `${ziel === "firmware" ? "Übertrage Firmware" : "Übertrage Oberfläche"} … ${Math.round(e.loaded / e.total * 100)} %`);
    };
    x.onload = () => { let j = {}; try { j = JSON.parse(x.responseText); } catch (e) { /* leer */ } x.status < 300 && j.ok ? ok(j) : fehl(new Error(j.text || `Fehler ${x.status}`)); };
    x.onerror = () => fehl(new Error("Verbindung zur Waage abgebrochen"));
    x.send(fd);
  });
}
async function updateEinspielen(datei) {
  $("e-update").value = "";
  try {
    updateStand(0, "Lese Datei …");
    const text = await datei.text();
    const i = text.indexOf("const TEILE = [");
    if (i < 0) throw new Error("Das ist keine FilamentLager-Webflasher.html");
    const start = i + "const TEILE = ".length, ende = text.indexOf("];", start) + 1;
    const teile = JSON.parse(text.slice(start, ende));
    const version = (text.match(/FW ([0-9][0-9.]*)/) || [])[1] || "?";
    const fw = teile.find((t) => t.name === "firmware.bin"), fs = teile.find((t) => t.name.startsWith("littlefs"));
    const info = await (await fetch("/api/update/info")).json();
    if (!fw) throw new Error("In der Datei fehlt die Firmware");
    if (!info.ota) throw new Error("Diese Waage kann sich noch nicht selbst aktualisieren – einmal per USB mit dem Webflasher flashen");
    if (fs && fs.adresse !== info.fs_offset) throw new Error(`Version ${version} teilt den Speicher anders auf – bitte einmal per USB mit dem Webflasher flashen`);
    if (fw.b64.length * 3 / 4 > info.app_max) throw new Error("Die Firmware ist zu groß für diese Waage");
    meldung(`Spiele Version ${version} ein …`);
    if (fs) await hochladen("oberflaeche", fs, 0, 30);
    await hochladen("firmware", fw, fs ? 30 : 0, 100);
    updateStand(100, `Version ${version} eingespielt – die Waage startet neu, die Seite lädt gleich neu`);
    setTimeout(() => location.reload(), 15000);
  } catch (e) {
    S.update = null;
    meldung(e.message, true);
    if (S.seite === "einst") einst();
  }
}

// ------------------------------------------------------------ Daten vom Dienst
async function lagerLaden() {
  let d;
  try {
    const r = await fetch("/api/lager", { signal: AbortSignal.timeout(20000) });
    d = await r.json();
  } catch (e) {
    // nochmal versuchen: zustand() lädt neu, sobald die Version nicht mehr passt
    setTimeout(() => { S.lagerVersion = -1; }, 3000);
    return;
  }
  S.sorten = d.sorten;
  S.lagerVersion = d.version;
  S.karteSchluessel = "";
  etLaden();
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
  else if (S.seite === "einst") einstMenue();
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
einstLaden();
etLaden();
amsLaden();
setInterval(amsLaden, 30000);
