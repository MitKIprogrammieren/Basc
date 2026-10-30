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
// Im Kiosk auf dem Pi (Adresse mit ?kiosk) liegt die Textgröße beim Dienst: der Browser startet dort
// jedes Mal mit leerem Profil. Ohne Wahl ist sie im Kiosk „Groß“ – das TFT ist klein.
const KIOSK = new URLSearchParams(location.search).has("kiosk");
function textGroesse() {
  if (KIOSK) return (typeof S !== "undefined" && S.e && S.e.textgroesse_kiosk) || "gross";
  try { return localStorage.getItem("fl_textgroesse") || "normal"; } catch (e) { return "normal"; }
}
function textFaktor() { return (TEXTGROESSEN.find((t) => t[0] === textGroesse()) || TEXTGROESSEN[1])[2]; }
function skalieren() {
  const t = textFaktor(), W = Math.round(800 / t), H = Math.round(480 / t);
  const f = Math.min(innerWidth / W, innerHeight / H);
  const app = $("app");
  app.style.width = `${W}px`;
  app.style.height = `${H}px`;
  app.classList.toggle("eng", W < 760);   // „Groß“: weniger Platz, die festen Maße rücken zusammen
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

// ------------------------------------------------------------ Filamentrolle statt Farbquadrat (wie in SchichtLohn)
// Die Muster (Seide, Metallic, CF, GF, Glitzer, Transparent) liegen einmal im Dokument, jede Rolle verweist nur darauf.
function rollenMuster() {
  if (document.getElementById("rollen-muster")) return;
  const d = document.createElement("div");
  d.id = "rollen-muster";
  d.style.cssText = "position:absolute;width:0;height:0;overflow:hidden";
  d.innerHTML = `<svg width="0" height="0" aria-hidden="true"><defs>
<linearGradient id="rlSh" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset=".35" stop-color="#fff" stop-opacity="0"/><stop offset=".7" stop-color="#000" stop-opacity=".18"/><stop offset="1" stop-color="#000" stop-opacity=".38"/></linearGradient>
<linearGradient id="rlShM" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#000" stop-opacity=".02"/><stop offset=".6" stop-color="#000" stop-opacity=".12"/><stop offset="1" stop-color="#000" stop-opacity=".3"/></linearGradient>
<linearGradient id="rlSilk" x1="0" y1="0" x2="1" y2="1"><stop offset=".22" stop-color="#fff" stop-opacity="0"/><stop offset=".42" stop-color="#fff" stop-opacity=".62"/><stop offset=".5" stop-color="#fff" stop-opacity=".18"/><stop offset=".6" stop-color="#fff" stop-opacity=".45"/><stop offset=".78" stop-color="#fff" stop-opacity="0"/></linearGradient>
<linearGradient id="rlMetal" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".45"/><stop offset=".3" stop-color="#000" stop-opacity=".12"/><stop offset=".5" stop-color="#fff" stop-opacity=".35"/><stop offset=".75" stop-color="#000" stop-opacity=".2"/><stop offset="1" stop-color="#fff" stop-opacity=".25"/></linearGradient>
<pattern id="rlCf" width="2.8" height="2.8" patternUnits="userSpaceOnUse" patternTransform="rotate(32)"><path d="M.1 .5h1.1M1.3 1.75h.8" stroke="#000" stroke-opacity=".26" stroke-width=".3"/><path d="M.6 1.15h.6" stroke="#fff" stroke-opacity=".1" stroke-width=".25"/></pattern>
<pattern id="rlGf" width="2.8" height="2.8" patternUnits="userSpaceOnUse" patternTransform="rotate(-28)"><path d="M.1 .5h1.1M1.3 1.75h.8" stroke="#fff" stroke-opacity=".3" stroke-width=".26"/><path d="M.6 1.15h.5" stroke="#000" stroke-opacity=".08" stroke-width=".25"/></pattern>
<pattern id="rlChk" width="3" height="3" patternUnits="userSpaceOnUse"><rect width="3" height="3" fill="#c9cdd3"/><rect width="1.5" height="1.5" fill="#868c94"/><rect x="1.5" y="1.5" width="1.5" height="1.5" fill="#868c94"/></pattern>
<pattern id="rlGlit" width="3.2" height="3.2" patternUnits="userSpaceOnUse"><circle cx=".7" cy=".8" r=".32" fill="#fff" fill-opacity=".95"/><circle cx="2.3" cy="2.2" r=".22" fill="#fff" fill-opacity=".75"/><circle cx="2.6" cy=".5" r=".16" fill="#ffe9a8" fill-opacity=".85"/></pattern>
</defs></svg>`;
  document.body.appendChild(d);
}
/** Filamentrolle als SVG. groesse: CSS-Klasse r22 / r30 / r34 / r64 */
function rolle(hex, bes, groesse = "r30") {
  rollenMuster();
  const c = /^#[0-9a-f]{6}/i.test(String(hex || "")) ? String(hex).slice(0, 7) : "#9e9e9e";
  const fl = (bes || []).map((x) => String(x).toLowerCase());
  const hat = (re) => fl.some((x) => re.test(x));
  const trans = hat(/^(transparent|klar|translucent)$/), matt = hat(/^matte?$/), seide = hat(/^(seide|silk)$/),
    cf = hat(/^cf$/), gf = hat(/^gf$/), glitzer = hat(/^(glitzer|sparkle|galaxy)$/), metal = hat(/^(metallic|metall)$/);
  const ov = [];
  if (seide) ov.push("url(#rlSilk)"); if (metal) ov.push("url(#rlMetal)"); if (cf) ov.push("url(#rlCf)");
  if (gf) ov.push("url(#rlGf)"); if (glitzer) ov.push("url(#rlGlit)");
  const BAND = "M18 6.4H27A8.3 15.6 0 0 1 27 37.6H18Z";
  const lag = (form) => ov.map((u) => form.replace("FILL", u)).join("");
  const op = trans ? ' fill-opacity=".58"' : "";
  return `<span class="rolle ${groesse}" role="img" aria-label="Filamentfarbe"><svg viewBox="0 0 44 44" aria-hidden="true"><g transform="rotate(-16 22 22)" stroke-linejoin="round">
<ellipse cx="27" cy="22" rx="9.4" ry="17.4" fill="#2b2f35" stroke="#7d8591" stroke-width="1"/>
${trans ? `<path d="${BAND}" fill="url(#rlChk)"/>` : ""}<path d="${BAND}" fill="${c}"${op}/>
<path d="${BAND}" fill="url(#${matt ? "rlShM" : "rlSh"})"/>${lag(`<path d="${BAND}" fill="FILL"/>`)}
<path d="M21.5 6.4A8.3 15.6 0 0 1 21.5 37.6M25 6.4A8.3 15.6 0 0 1 25 37.6" fill="none" stroke="#000" stroke-opacity=".1" stroke-width=".55"/>
<path d="M18 6.4H27A8.3 15.6 0 0 1 27 37.6H18" fill="none" stroke="#000" stroke-opacity=".45" stroke-width=".7" stroke-linecap="round"/>
<ellipse cx="18" cy="22" rx="9.4" ry="17.4" fill="#33373e" stroke="#a3abb6" stroke-width="1"/>
${trans ? `<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="url(#rlChk)"/>` : ""}<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="${c}"${op}/>
${matt ? `<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="#000" fill-opacity=".06"/>` : ""}${lag(`<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="FILL"/>`)}
<ellipse cx="18" cy="22" rx="6.9" ry="12.9" fill="none" stroke="#000" stroke-opacity=".14" stroke-width=".6"/>
<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="none" stroke="#000" stroke-opacity=".35" stroke-width=".7"/>
<ellipse cx="18" cy="22" rx="5.2" ry="9.6" fill="#4b5058" stroke="#000" stroke-opacity=".45" stroke-width=".8"/>
<ellipse cx="18" cy="22" rx="2.6" ry="4.8" fill="#1d2024" stroke="#a3abb6" stroke-width=".7"/>
<ellipse cx="18" cy="22" rx="1" ry="1.9" fill="#0b0c0e"/>
${matt ? "" : `<path d="M12.2 11.6c1.3-2.9 3.1-4.7 4.9-5.1" stroke="#fff" stroke-opacity="${seide || metal ? ".85" : ".5"}" stroke-width="${seide || metal ? "1.3" : ".9"}" fill="none" stroke-linecap="round"/>`}</g></svg></span>`;
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
  const rk = sorte ? sorte.uid : "";
  if ($("spule").dataset.k !== rk || !$("spule").firstChild) {  // nur neu zeichnen, wenn sich die Sorte ändert
    $("spule").dataset.k = rk;
    $("spule").innerHTML = sorte ? rolle(sorte.hex, sorte.besonderheiten, "rgross") : rolle("#3a4048", ["matt"], "rgross leer");
  }
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
    $("k-swatch").className = "k-rolle";
    $("k-swatch").style.background = "";
    $("k-swatch").innerHTML = rolle(sorte.hex, sorte.besonderheiten, "r34");
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
    $("k-swatch").className = "swatch s34";
    $("k-swatch").innerHTML = "";
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
    $("k-swatch").className = "swatch s34";
    $("k-swatch").innerHTML = "";
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
// Herstellerlogo aus Home Assistant (config/www/hersteller → /local/hersteller), über die Waage geholt.
// Fehlt das Bild, bleibt der Name stehen.
function herstellerLogo(name, frage) {
  if (!name) return "";
  const v = S.et && S.et.bilder ? S.et.bilder[name] || 0 : 0;
  return `<span class="hlogo"><img src="/api/hersteller_bild?name=${encodeURIComponent(name)}${v ? `&v=${v}` : ""}" alt="" loading="lazy"
    onload="logoFaerben(this)" onerror="this.parentNode.classList.add('fehlt')"><span class="hname">${frage ? markiere(name, frage) : esc(name)}</span></span>`;
}
const logosAn = () => S.lager.logos !== false;

// Logo in Originalfarben, aber ohne weißen Hintergrund (durchsichtig).
const LOGO_FARBIG = new Map();
function logoFaerben(img) {
  const box = img.parentNode;
  if (!img.naturalWidth) { box.classList.add("fehlt"); return; }
  if (img.dataset.fertig) return;
  const farbe = getComputedStyle(box).color;
  const schluessel = img.src + "|" + farbe;
  const fertig = (url) => { img.dataset.fertig = "1"; img.src = url; box.classList.add("bereit"); };
  if (LOGO_FARBIG.has(schluessel)) { fertig(LOGO_FARBIG.get(schluessel)); return; }
  try {
    const sk = Math.min(1, 240 / img.naturalHeight, 600 / img.naturalWidth);
    const w = Math.max(1, Math.round(img.naturalWidth * sk)), h = Math.max(1, Math.round(img.naturalHeight * sk));
    const c = document.createElement("canvas"); c.width = w; c.height = h;
    const x = c.getContext("2d", { willReadFrequently: true });
    x.drawImage(img, 0, 0, w, h);
    const d = x.getImageData(0, 0, w, h), p = d.data;
    // Weißen/hellen Hintergrund vom Rand her entfernen (Flutfüllung) – Farben des Logos bleiben original.
    const hell = (i) => {
      const r = p[i * 4], g = p[i * 4 + 1], b = p[i * 4 + 2];
      return p[i * 4 + 3] > 0 && Math.min(r, g, b) > 200 && Math.max(r, g, b) - Math.min(r, g, b) < 30;
    };
    const weg = new Uint8Array(w * h), stapel = [];
    for (let i = 0; i < w; i++) stapel.push(i, (h - 1) * w + i);
    for (let j = 0; j < h; j++) stapel.push(j * w, j * w + w - 1);
    while (stapel.length) {
      const i = stapel.pop();
      if (weg[i] || !(p[i * 4 + 3] < 30 || hell(i))) continue;
      weg[i] = 1;
      const xx = i % w, yy = (i - xx) / w;
      if (xx > 0) stapel.push(i - 1); if (xx < w - 1) stapel.push(i + 1);
      if (yy > 0) stapel.push(i - w); if (yy < h - 1) stapel.push(i + w);
    }
    // Kantenglättung: Pixel neben dem Hintergrund je nach Helligkeit teilweise durchsichtig
    for (let i = 0; i < w * h; i++) {
      if (weg[i]) { p[i * 4 + 3] = 0; continue; }
      const xx = i % w, yy = (i - xx) / w;
      const rand = (xx > 0 && weg[i - 1]) || (xx < w - 1 && weg[i + 1]) || (yy > 0 && weg[i - w]) || (yy < h - 1 && weg[i + w]);
      if (rand) {
        const lum = (0.299 * p[i * 4] + 0.587 * p[i * 4 + 1] + 0.114 * p[i * 4 + 2]) / 255;
        p[i * 4 + 3] = Math.round(p[i * 4 + 3] * Math.min(1, Math.max(0, (1 - lum) / 0.25)));
      }
    }
    // Schwarze/dunkelgraue Logos wären auf dem dunklen Hintergrund unsichtbar:
    // nur unbunte dunkle Pixel in Schriftfarbe aufhellen, bunte Teile bleiben wie sie sind.
    let dunkel = 0, sichtbar = 0;
    for (let i = 0; i < p.length; i += 4) if (p[i + 3] > 128) {
      sichtbar++;
      if (0.299 * p[i] + 0.587 * p[i + 1] + 0.114 * p[i + 2] < 90) dunkel++;
    }
    if (sichtbar && dunkel / sichtbar > 0.5) {
      const m = farbe.match(/\d+(\.\d+)?/g) || ["230", "230", "230"];
      const [fr, fg, fb] = m.map(Number);
      for (let i = 0; i < p.length; i += 4) {
        const r = p[i], g = p[i + 1], b = p[i + 2];
        if (Math.max(r, g, b) - Math.min(r, g, b) < 40 && 0.299 * r + 0.587 * g + 0.114 * b < 140) { p[i] = fr; p[i + 1] = fg; p[i + 2] = fb; }
      }
    }
    x.putImageData(d, 0, 0);
    const url = c.toDataURL("image/png");
    LOGO_FARBIG.set(schluessel, url);
    fertig(url);
  } catch (e) { box.classList.add("bereit"); }
}

// ------------------------------------------------------------ Lager: Suche, Filter, Sortierung
// Einstellungen je Gerät: Stil der Filter (Auswahlliste oder Knöpfe wie früher) und Sortierung
function lagerOpt() {
  let o = null;
  try { o = JSON.parse(localStorage.getItem("fl_lager") || "null"); } catch (e) { /* ohne Speicher */ }
  o = o && typeof o === "object" ? o : {};
  if (!["liste", "knoepfe"].includes(o.stil)) o.stil = "liste";
  if (!["treffer", "name", "hersteller", "bestand", "farbe"].includes(o.sort)) o.sort = "name";
  if (typeof o.mat !== "string") o.mat = "alle";
  return o;
}
S.lager = lagerOpt();
function lagerOptSpeichern() { try { localStorage.setItem("fl_lager", JSON.stringify(S.lager)); } catch (e) { /* nur bis zum Neuladen */ } }

const normal = (t) => String(t || "").toLowerCase().replace(/ä/g, "ae").replace(/ö/g, "oe").replace(/ü/g, "ue").replace(/ß/g, "ss")
  .normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/[^a-z0-9#+]+/g, " ").trim();
// Farben und Begriffe deutsch ↔ englisch – jede Gruppe gilt als gleichwertig
const SYNONYME = [["schwarz", "black", "noir"], ["weiss", "white", "weis"], ["grau", "gray", "grey"], ["rot", "red"],
  ["blau", "blue"], ["gruen", "green"], ["gelb", "yellow"], ["orange"], ["lila", "violett", "purple", "violet"],
  ["rosa", "pink", "magenta"], ["braun", "brown"], ["silber", "silver"], ["gold", "golden"], ["beige", "creme", "cream", "ivory", "elfenbein"],
  ["transparent", "klar", "clear", "translucent", "durchsichtig"], ["natur", "natural", "naturel"], ["holz", "wood"],
  ["seide", "silk"], ["matt", "matte"], ["glitzer", "sparkle", "galaxy", "glitter"], ["metall", "metallic", "metal"],
  ["carbon", "cf", "kohlefaser"], ["glas", "gf", "glasfaser"], ["leuchtend", "glow", "nachtleuchtend"], ["marmor", "marble"]];
const SYN = new Map();
for (const g of SYNONYME) for (const w of g) SYN.set(w, g);
const STICHWORTE = { angebrochen: (s) => s.reste.length > 0, rest: (s) => s.reste.length > 0, reste: (s) => s.reste.length > 0,
  leer: (s) => s.spulen === 0, aufgebraucht: (s) => s.spulen === 0, voll: (s) => s.spulen > 0 && !s.reste.length,
  nfc: (s) => s.nfc.length > 0 };

function suchIndex(s) {
  if (s._idx) return s._idx;
  const felder = [[s.hersteller, 3], [s.material, 3], [s.farbe, 3], [(s.besonderheiten || []).join(" "), 2], [s.summary, 1],
    [s.fcode, 2], [s.bambu_id, 1], [(s.codes || []).join(" "), 1], [s.ort, 2], [(s.nfc || []).join(" "), 1]];
  const tok = [];
  for (const [t, g] of felder) for (const w of normal(t).split(" ")) if (w) tok.push([w, g]);
  return (s._idx = tok);
}
function abstand(a, b, max) {  // Levenshtein mit frühem Abbruch
  if (Math.abs(a.length - b.length) > max) return max + 1;
  let v = Array.from({ length: b.length + 1 }, (_, i) => i);
  for (let i = 1; i <= a.length; i++) {
    const n = [i];
    let klein = i;
    for (let j = 1; j <= b.length; j++) {
      n[j] = Math.min(v[j] + 1, n[j - 1] + 1, v[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
      klein = Math.min(klein, n[j]);
    }
    if (klein > max) return max + 1;
    v = n;
  }
  return v[b.length];
}
function synonymGruppe(wort) {
  if (SYN.has(wort)) return SYN.get(wort);
  if (wort.length < 4) return null;
  const grenze = wort.length >= 7 ? 2 : 1;  // Tippfehler: „schwraz“ → schwarz/black
  for (const [k, g] of SYN) if (k.length >= 4 && abstand(wort, k, grenze) <= grenze) return [wort, ...g];
  return null;
}
function wortPunkte(idx, wort) {
  const varianten = synonymGruppe(wort) || [wort];
  let best = 0;
  for (const [t, g] of idx) {
    for (const w of varianten) {
      let p = 0;
      if (t === w) p = 10;
      else if (t.startsWith(w)) p = 6;
      else if (w.length >= 3 && t.includes(w)) p = 3;
      else if (w.length >= 4 && abstand(w, t.slice(0, w.length + 1), w.length >= 7 ? 2 : 1) <= (w.length >= 7 ? 2 : 1)) p = 2;
      best = Math.max(best, p * g);
    }
  }
  return best;
}
/** Treffer mit Punktzahl oder null. Stichwörter (angebrochen, leer, voll, nfc, „ohne nfc“) filtern direkt. */
function suchTreffer(s, frage) {
  const woerter = normal(frage).split(" ").filter(Boolean);
  let punkte = 0;
  for (let i = 0; i < woerter.length; i++) {
    const w = woerter[i];
    if (w === "ohne" && woerter[i + 1] === "nfc") { if (s.nfc.length) return null; i++; continue; }
    if (STICHWORTE[w]) { if (!STICHWORTE[w](s)) return null; punkte += 5; continue; }
    const p = wortPunkte(suchIndex(s), w);
    if (!p) return null;
    punkte += p;
  }
  return punkte;
}
function markiere(text, frage) {
  let h = esc(text);
  const woerter = normal(frage).split(" ").filter((w) => w.length >= 2 && !STICHWORTE[w] && w !== "ohne");
  const alle = [...new Set(woerter.flatMap((w) => synonymGruppe(w) || [w]))].sort((a, b) => b.length - a.length);
  if (!alle.length) return h;
  const muster = alle.map((w) => w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&").replace(/ae/g, "(?:ae|ä)").replace(/oe/g, "(?:oe|ö)")
    .replace(/ue/g, "(?:ue|ü)").replace(/ss/g, "(?:ss|ß)")).join("|");
  return h.replace(new RegExp(`(${muster})`, "gi"), "<mark>$1</mark>");
}
function farbton(hex) {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(hex || "");
  if (!m) return [9, 0];
  const [r, g, b] = [m[1], m[2], m[3]].map((x) => parseInt(x, 16) / 255);
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn, l = (mx + mn) / 2;
  if (d < 0.08) return [8, 1 - l];  // Grautöne ans Ende, hell nach dunkel
  let h = mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return [Math.floor(((h * 60 + 360) % 360) / 30), l];
}
const materialName = (s) => (s.material || "Ohne Material").trim().toUpperCase();
function materialListe() {
  const z = new Map();
  for (const s of S.sorten) z.set(materialName(s), (z.get(materialName(s)) || 0) + 1);
  return [...z.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
}
function passtMaterial(s) {
  if (S.lager.stil === "knoepfe") return passtFilter(s, S.filter);
  if (S.lager.mat === "alle") return true;
  if (S.lager.mat === "angebrochen") return s.reste.length > 0;
  return materialName(s) === S.lager.mat;
}
const SORTIERUNG = [["treffer", "Treffer"], ["name", "Name"], ["hersteller", "Hersteller"], ["bestand", "Bestand"], ["farbe", "Farbe"]];
function sortieren(liste, frage) {
  const art = frage && S.lager.sortWahl !== true ? "treffer" : S.lager.stil === "knoepfe" ? "keine" : S.lager.sort;
  const nach = (f) => (a, b) => String(f(a.s)).localeCompare(String(f(b.s)), "de", { numeric: true, sensitivity: "base" });
  const vgl = {
    treffer: (a, b) => b.p - a.p || nach(kurzname)(a, b),
    name: nach((s) => `${kurzname(s)} ${s.hersteller}`),
    hersteller: nach((s) => `${s.hersteller} ${kurzname(s)}`),
    bestand: (a, b) => (a.s.spulen === 0) - (b.s.spulen === 0) || a.s.bestand_g - b.s.bestand_g || nach(kurzname)(a, b),
    farbe: (a, b) => { const x = farbton(a.s.hex), y = farbton(b.s.hex); return x[0] - y[0] || x[1] - y[1]; },
  }[art] || (() => 0);
  return liste.sort(vgl);
}
let ddOffen = "";
const DD_ICON = {
  runter: '<path d="m6 9 6 6 6-6"/>', hoch: '<path d="m6 15 6-6 6 6"/>',
  sortieren: '<path d="M7 4v16M3 8l4-4 4 4"/><path d="M14 6h7M14 12h5M14 18h3"/>',
};
const ddIcon = (n) => `<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${DD_ICON[n]}</svg>`;
function filterLeiste() {
  const f = $("filter");
  f.className = S.lager.stil === "knoepfe" ? "reihe gap8" : "reihe gap8 dd-reihe";
  if (S.lager.stil === "knoepfe") {
    f.innerHTML = FILTER.map((x, i) => `<button class="filter${i === S.filter ? " an" : ""}" data-f="${i}">${x}</button>`).join("");
    return;
  }
  const mats = materialListe();
  const aktMat = S.lager.mat === "alle" ? ["Alle", S.sorten.length] : S.lager.mat === "angebrochen"
    ? ["Angebrochen", S.sorten.filter((s) => s.reste.length).length] : [S.lager.mat, (mats.find(([m]) => m === S.lager.mat) || [0, 0])[1]];
  const sucht = $("suche").value.trim() && S.lager.sortWahl !== true;
  const sortName = SORTIERUNG.find(([k]) => k === (sucht ? "treffer" : S.lager.sort))[1];
  f.innerHTML = `<div class="dd-halter"><button class="dd${ddOffen === "mat" ? " auf" : ""}" data-dd="mat"><small>Material</small><b>${esc(aktMat[0])}</b><span class="dd-n">${aktMat[1]}</span>${ddIcon(ddOffen === "mat" ? "hoch" : "runter")}</button>${ddOffen === "mat" ? ddPanel("mat", mats) : ""}</div>
    <div class="dd-halter"><button class="dd${ddOffen === "sort" ? " auf" : ""}" data-dd="sort"><small>Sortieren</small><b>${sortName}</b>${ddIcon(ddOffen === "sort" ? "hoch" : "runter")}</button>${ddOffen === "sort" ? ddPanel("sort") : ""}</div>`;
}
function ddPanel(art, mats) {
  if (art === "sort") return `<div class="dd-panel rechts">${SORTIERUNG.map(([k, t]) =>
    `<button class="dd-o${S.lager.sort === k ? " an" : ""}" data-sort="${k}"><span>${t}</span><small>${{ treffer: "beim Suchen", bestand: "wenig zuerst", farbe: "nach Farbton" }[k] || ""}</small></button>`).join("")}</div>`;
  const anz = S.sorten.filter((s) => s.reste.length).length;
  return `<div class="dd-panel">${[["alle", "Alle", S.sorten.length], ["angebrochen", "Angebrochen", anz]].map(([k, t, n]) =>
    `<button class="dd-o${S.lager.mat === k ? " an" : ""}" data-mat="${k}"><span>${t}</span><small>${n}</small></button>`).join("")}
    <div class="dd-trenner"></div>${mats.map(([m, n]) =>
    `<button class="dd-o${S.lager.mat === m ? " an" : ""}" data-mat="${esc(m)}"><span>${esc(m)}</span><small>${n}</small></button>`).join("")}</div>`;
}
$("filter").onclick = (e) => {
  const d = e.target.closest("[data-dd]"), m = e.target.closest("[data-mat]"), so = e.target.closest("[data-sort]"), b = e.target.closest("[data-f]");
  if (d) { ddOffen = ddOffen === d.dataset.dd ? "" : d.dataset.dd; filterLeiste(); return; }
  if (m) { S.lager.mat = m.dataset.mat; ddOffen = ""; lagerOptSpeichern(); return liste(); }
  if (so) { S.lager.sort = so.dataset.sort; S.lager.sortWahl = $("suche").value.trim() ? true : undefined; ddOffen = ""; lagerOptSpeichern(); return liste(); }
  if (b) { S.filter = Number(b.dataset.f); liste(); }
};
document.addEventListener("click", (e) => {
  // composedPath statt closest: das angeklickte Element ist nach dem Neuzeichnen schon aus dem Dokument
  if (ddOffen && !e.composedPath().includes($("filter"))) { ddOffen = ""; filterLeiste(); }
});

function liste() {
  filterLeiste();
  const q = $("suche").value.trim();
  if (!q) delete S.lager.sortWahl;
  let n = 0, weitere = 0;
  const html = [];
  const vorschlag = S.auswahlModus && S.vorschlag ? S.vorschlag : null;
  const treffer = [];
  for (const s of S.sorten) {
    if (s === vorschlag) continue;
    if (!passtMaterial(s)) continue;
    const p = q ? suchTreffer(s, q) : 0;
    if (p === null) continue;
    treffer.push({ s, p });
  }
  sortieren(treffer, q);
  const reihe = vorschlag ? [vorschlag, ...treffer.map((t) => t.s)] : treffer.map((t) => t.s);
  for (const s of reihe) {
    if (n >= MAX_ZEILEN) { weitere++; continue; }
    n++;
    html.push(`<div class="zeile${s === vorschlag ? " vorschlag" : ""}" data-uid="${esc(s.uid)}">
      ${rolle(s.hex, s.besonderheiten, "r30")}
      <div class="spalte gap2 wachsen minw0"><span class="ellipse" style="font-weight:600;font-size:15px">${markiere(kurzname(s), q)}</span>
        <span class="sub14 ellipse unterzeile">${logosAn() && s.hersteller ? herstellerLogo(s.hersteller, q) + markiere([...s.besonderheiten.slice(0, 2), s.ort].filter(Boolean).map((x) => ` · ${x}`).join(""), q)
          : markiere(unterzeile(s) + (s.ort ? ` · ${s.ort}` : ""), q)}</span></div>
      ${s === vorschlag ? '<span class="chip ams">Vorschlag aus AMS</span>' : s.nfc.length ? '<span class="chip b">NFC</span>' : ""}
      <div class="rechts"><div class="anz${s.spulen ? "" : " leer"}">${spulenText(s.spulen)}</div>
        <div class="sub14">${esc(restText(s))}</div></div></div>`);
  }
  if (q) $("suche-n").textContent = `${treffer.length} Treffer`; else $("suche-n").textContent = "";
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
      ${rolle(s.hex, s.besonderheiten, "r22")}
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
        <div class="spalte minw0"><span class="name20 ellipse">${esc(kurzname(s))}</span><span class="sub14">${logosAn() ? herstellerLogo(s.hersteller) : esc(s.hersteller)}</span></div></div>
      <div class="reihe gap12 mitte">${rolle(s.hex, s.besonderheiten, "r64")}
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
  bruecke: '<path d="M2 18h20"/><path d="M4 18v-6a8 8 0 0 1 16 0v6"/><path d="M8 18v-5M12 18v-6M16 18v-5"/>',
  system: '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M9 9h6v6H9z"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M1 9h3M1 15h3M20 9h3M20 15h3"/>',
};
const MENUE = [
  ["design", "Design"], ["wlan", "WLAN"], ["ha", "Home Assistant"], ["bambu", "Bambu Lab"], ["waage", "Waage"], ["etikett", "Etiketten"], ["bruecke", "Druckbrücke"], ["system", "System"],
];
// Die Druckbrücke steht nur im Menü, wenn unter Etiketten eine eingetragen ist
const menueListe = () => MENUE.filter(([n]) => n !== "bruecke" || (S.et && S.et.bruecke));
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
    if (KIOSK) skalieren();
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
  logoSetzen(f);
}
// Logo und Favicon: eine Filamentrolle in der Akzentfarbe
function logoSetzen(farbe) {
  const l = document.querySelector(".kopf .logo");
  if (l && l.dataset.f !== farbe) { l.dataset.f = farbe; l.innerHTML = rolle(farbe, [], "rlogo"); }
  let fav = document.querySelector('link[rel="icon"]');
  if (!fav) { fav = document.createElement("link"); fav.rel = "icon"; fav.type = "image/svg+xml"; document.head.appendChild(fav); }
  fav.href = "data:image/svg+xml," + encodeURIComponent(favicon(farbe));
}
function favicon(c) {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 44 44"><g transform="rotate(-16 22 22)">
<ellipse cx="27" cy="22" rx="9.4" ry="17.4" fill="#2b2f35" stroke="#7d8591" stroke-width="1.4"/>
<path d="M18 6.4H27A8.3 15.6 0 0 1 27 37.6H18Z" fill="${c}"/>
<path d="M18 6.4H27A8.3 15.6 0 0 1 27 37.6H18" fill="none" stroke="#000" stroke-opacity=".45" stroke-width="1"/>
<ellipse cx="18" cy="22" rx="9.4" ry="17.4" fill="#33373e" stroke="#a3abb6" stroke-width="1.4"/>
<ellipse cx="18" cy="22" rx="8.3" ry="15.6" fill="${c}"/>
<ellipse cx="18" cy="22" rx="5.2" ry="9.6" fill="#4b5058" stroke="#000" stroke-opacity=".45"/>
<ellipse cx="18" cy="22" rx="2.6" ry="4.8" fill="#1d2024" stroke="#a3abb6" stroke-width=".9"/></g></svg>`;
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
    return `${S.et.bruecke ? "über Druckbrücke" : S.et.adresse ? S.et.name || S.et.adresse : "kein Drucker"} · ${f}`;
  }
  if (n === "bruecke") {
    const b = S.br;
    if (!b) return "wird geladen …";
    if (b.fehler) return "nicht erreichbar";
    const r = b.rolle || {};
    return [b.verbunden ? "verbunden" : "nicht verbunden", r.rest != null ? `Rolle ${r.rest}${r.gesamt ? "/" + r.gesamt : ""}` : ""].filter(Boolean).join(" · ");
  }
  if (n === "waage") return `Leerspule ${e.leerspule_g ?? 250} g · NFC Stufe ${e.nfc_stufe ?? 3}`;
  return `Version ${z.version || ""}`;
}
function einstMenue() {
  $("e-menue").innerHTML = menueListe().map(([n, t]) => `<button class="m-zeile${S.eSeite === n ? " an" : ""}" data-m="${n}">
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
  if (S.eSeite === "bruecke") brLaden();
  einst();
};

// ------------------------------------------------------------ System-Info (Temperatur, Leistung, Speicher)
S.sys = null;
let sysTimer = null;
async function sysLaden() {
  if (S.seite !== "einst" || S.eSeite !== "system") { clearInterval(sysTimer); sysTimer = null; return; }
  try {
    const r = await fetch("/api/system/info", { cache: "no-store" });
    if (!r.ok) throw new Error(r.status);
    S.sys = await r.json();
  } catch (e) { S.sys = S.sys || { fehlt: true }; }
  const z = $("sys-info");
  if (z) z.outerHTML = sysInfoHtml();
}
function sysBeobachten() {
  if (S.seite === "einst" && S.eSeite === "system" && !sysTimer) { sysLaden(); sysTimer = setInterval(sysLaden, 3000); }
}
const groesse = (b) => (b >= 1073741824 ? `${komma(b / 1073741824, 1)} GB` : b >= 1048576 ? `${komma(b / 1048576, b >= 10485760 ? 0 : 2)} MB` : `${Math.round(b / 1024)} KB`);
function dauer(s) {
  const t = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return t ? `${t} T ${h} Std` : h ? `${h} Std ${m} Min` : `${m} Min`;
}
function sysBalken(p, warn = 75, alarm = 90) {
  const f = p >= alarm ? "var(--rot)" : p >= warn ? "var(--orange)" : "var(--gruen)";
  return `<div class="sys-bar"><i style="width:${Math.max(2, Math.min(100, p))}%;background:${f}"></i></div>`;
}
function sysInfoHtml() {
  const d = S.sys;
  if (!d) return '<div id="sys-info" class="sub14">Systemwerte werden geladen …</div>';
  if (d.fehlt) return '<div id="sys-info" class="sub14">Systemwerte gibt es ab Firmware 2.11.</div>';
  const k = [];
  if (d.temperatur_c != null) {
    const t = d.temperatur_c, txt = t >= 80 ? "sehr heiß" : t >= 65 ? "warm" : "normal";
    k.push(`<div class="sys-k"><span class="ueber">Temperatur</span><b>${komma(t, 0)} °C</b><small>Chip · ${txt}</small>${sysBalken(t, 65, 80)}</div>`);
  }
  if (d.cpu_mhz) {
    const unter = d.last != null ? `Auslastung ${d.last} %` : d.schleife_pro_s != null ? `Hauptschleife ${d.schleife_pro_s}/s` : `${d.kerne} Kerne`;
    k.push(`<div class="sys-k"><span class="ueber">Leistung</span><b>${d.cpu_mhz} MHz</b><small>${d.kerne || 1} Kerne · ${unter}</small>${d.last != null ? sysBalken(d.last) : ""}</div>`);
  }
  if (d.wlan_rssi != null) {
    const r = d.wlan_rssi, q = r >= -55 ? "sehr gut" : r >= -67 ? "gut" : r >= -75 ? "mäßig" : "schwach";
    k.push(`<div class="sys-k"><span class="ueber">WLAN</span><b>${r} dBm</b><small>${q}${d.sendeleistung_dbm != null ? ` · ${komma(d.sendeleistung_dbm, 1)} dBm Sendeleistung` : ""}</small>${sysBalken(Math.max(0, Math.min(100, (r + 90) * 2.5)), 101, 101)}</div>`);
  }
  const sp = (d.speicher || []).map((x) => {
    const p = x.gesamt ? Math.round(x.belegt * 100 / x.gesamt) : 0;
    return `<div class="sys-k"><span class="ueber">${esc(x.name)}</span><b>${groesse(x.gesamt - x.belegt)} frei</b>
      <small>von ${groesse(x.gesamt)} · ${p} % belegt${x.text ? ` · ${esc(x.text)}` : ""}</small>${sysBalken(p)}</div>`;
  }).join("");
  const zeilen = [d.laufzeit_s != null ? ["Läuft seit", dauer(d.laufzeit_s)] : null, d.neustart_grund ? ["Letzter Neustart", d.neustart_grund] : null,
    d.board ? ["Board", d.board] : null].filter(Boolean)
    .map(([n, w]) => `<div class="info-zeile"><span>${n}</span><span>${esc(w)}</span></div>`).join("");
  return `<div id="sys-info" class="spalte gap10">
    <span class="ueber">${esc(d.geraet || "Gerät")}</span><div class="sys-kacheln">${k.join("")}</div>
    <span class="ueber">Speicher</span><div class="sys-kacheln zwei">${sp}</div>
    ${zeilen ? `<div class="info-liste" style="flex:0 0 auto">${zeilen}</div>` : ""}</div>`;
}

function einst() {
  einstMenue();
  const e = S.e;
  const z = S.z || {};
  const ziel = $("e-inhalt");
  if (S.eSeite === "etikett") { ziel.innerHTML = etEinstHtml(); return; }
  if (S.eSeite === "bruecke") { ziel.innerHTML = brEinstHtml(); return; }
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
      <div class="e-block"><span class="ueber">Lagerliste</span>${seg("kompakt", e.kompakt, [[false, "Normal"], [true, "Kompakt"]])}</div>
      <div class="e-block"><span class="ueber">Filter im Lager</span><div class="seg">${[["liste", "Auswahlliste"], ["knoepfe", "Knöpfe (wie bisher)"]].map(([k, t]) =>
        `<button class="${S.lager.stil === k ? "an" : ""}" data-lagerstil="${k}">${t}</button>`).join("")}</div>
        <span class="hinweis">Auswahlliste zeigt alle Materialien aus deinem Lager. Gilt nur für dieses Gerät.</span></div>
      <div class="e-block"><span class="ueber">Herstellerlogos im Lager</span><div class="seg">${[[true, "Logo"], [false, "Nur Name"]].map(([k, t]) =>
        `<button class="${logosAn() === k ? "an" : ""}" data-logos="${k}">${t}</button>`).join("")}</div>
        <span class="hinweis">Logos kommen aus Home Assistant (config/www/hersteller). Fehlt eins, steht der Name da.</span></div>`;
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
          <div class="ams">${a.slots.map((sl) => `<div class="slot${sl.leer ? " leer" : ""}">${sl.leer ? '<div class="spule-klein"></div>' : rolle(sl.hex, [], "r30")}
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
      ${sysInfoHtml()}
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
  sysBeobachten();
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
  if (d.br) return brAktion(d.br, b);
  if (d.akzent) return einstSetzen({ akzent: d.akzent });
  if (d.logos) { S.lager.logos = d.logos === "true"; lagerOptSpeichern(); return einst(); }
  if (d.lagerstil) { S.lager.stil = d.lagerstil; lagerOptSpeichern(); return einst(); }
  if (d.text) {
    if (KIOSK) { await einstSetzen({ textgroesse_kiosk: d.text }); skalieren(); return einst(); }
    try { localStorage.setItem("fl_textgroesse", d.text); } catch (e) { /* ohne Speicher nur bis zum Neuladen */ }
    skalieren(); return einst();
  }
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

// ------------------------------------------------------------ Druckbrücke (Phomemo M20 Pro)
// Die Oberfläche fragt die Brücke direkt (sie erlaubt das per CORS) – so geht es auf dem Pi und auf der Sensorplatine.
S.br = null;
let brTimer = 0;
async function brLaden() {
  clearTimeout(brTimer);
  const basis = S.et && S.et.bruecke;
  if (!basis) return;
  try {
    const ab = new AbortController(), t = setTimeout(() => ab.abort(), 4000);
    S.br = await (await fetch(basis + "/status", { cache: "no-store", signal: ab.signal })).json();
    clearTimeout(t);
  } catch (e) { S.br = { fehler: true }; }
  if (S.seite === "einst") { if (S.eSeite === "bruecke") einst(); else einstMenue(); }
  if (S.seite === "einst" && S.eSeite === "bruecke") brTimer = setTimeout(brLaden, 3000);
}
function brEinstHtml() {
  const b = S.br, basis = (S.et && S.et.bruecke) || "";
  const kopf = (p) => `<div class="e-zeile"><span class="e-titel wachsen">Druckbrücke${b && b.geraet && b.geraet.typ ? " · " + esc(b.geraet.typ) : ""}</span>${p}</div>`;
  if (!b) return kopf(pill(null, "lädt …")) + '<span class="sub14">Frage die Druckbrücke …</span>';
  if (b.fehler) return kopf(pill(false, "nicht erreichbar")) + `<span class="hinweis">${esc(basis)} antwortet nicht. Läuft der Dienst filamentwaage-druck auf dem Pi, und stimmt die Adresse unter Etiketten?</span>`;
  const st = b.verbunden ? pill(true, `verbunden seit ${b.verbunden_seit || ""}`)
    : b.freigegeben_s ? `<span class="status-pill frei">für die App frei · noch ${Math.ceil(b.freigegeben_s / 60)} min</span>` : pill(false, "nicht verbunden");
  const r = b.rolle || {}, ak = b.akku || {}, sg = b.signal || {}, dr = b.drucke || {}, g = b.geraet || {};
  const p = r.gesamt ? Math.round(r.rest * 100 / r.gesamt) : 0;
  const pFarbe = p < 10 ? "var(--rot)" : p < 25 ? "var(--orange)" : "var(--gruen)";
  const stufe = sg.rssi == null ? 0 : sg.rssi >= -3 ? 4 : sg.rssi >= -8 ? 3 : sg.rssi >= -15 ? 2 : 1;
  const akkuWert = ak.wert ? ak.wert + (/^\d+$/.test(ak.wert) && +ak.wert <= 100 ? " %" : "") : "";
  const kachel = (ue, gross, unten) => `<div class="br-k"><span class="ueber">${ue}</span><b class="br-gross">${gross}</b>${unten}</div>`;
  const letzte = (dr.letzte || []).slice(0, 5);
  const neuVerbinden = parseFloat(b.version || "0") >= 1.3;
  return kopf(st) + `
    <div class="br-kacheln">
      ${kachel("Etikettenrolle", r.rest != null ? `${r.rest}${r.gesamt ? ` <small>von ${r.gesamt}</small>` : ""}` : "–",
        `<div class="balken"><div style="width:${p}%;background:${pFarbe}"></div></div><span class="sub14">${r.rfid ? "RFID " + esc(r.rfid) : ""}</span>`)}
      ${akkuWert ? kachel("Akku", esc(akkuWert), `<span class="sub14">${ak.befehl ? "über SSSGET" + esc(ak.befehl) : ""}</span>`) : ""}
      ${kachel("Funksignal", esc(sg.text || "–"), `<span class="br-striche">${[6, 10, 14, 18].map((h, i) => `<i class="${i < stufe ? "an" : ""}" style="height:${h}px"></i>`).join("")}</span>
        <span class="sub14">${sg.rssi != null ? sg.rssi + " dB · " : ""}${dr.abbrueche_heute || 0} Abbrüche heute</span>`)}
      ${kachel("Gedruckt", `${dr.heute ?? 0} <small>heute</small>`, `<span class="sub14">${dr.gesamt ?? 0} insgesamt${dr.fehler_heute ? ` · ${dr.fehler_heute} Fehler heute` : ""}</span>`)}
    </div>
    <div class="e-block"><span class="ueber">Letzte Drucke</span>
      <div class="info-liste" style="flex:0 0 auto">${letzte.length ? letzte.map((e) => `<div class="info-zeile br-zeile"><span>${esc(String(e.zeit || "").slice(11, 16))} · ${esc(e.titel)}</span>
        <span class="${e.ok ? "br-ok" : "br-fehler"}">${esc(e.ok ? "gedruckt" : e.text)}</span></div>`).join("") : '<div class="info-zeile"><span>noch keine</span><span></span></div>'}</div></div>
    <span class="sub14">${esc([g.typ, g.firmware && "Firmware " + g.firmware, g.seriennummer && "SN " + g.seriennummer].filter(Boolean).join(" · "))}${b.version ? ` · Druckbrücke ${esc(b.version)}` : ""}</span>
    <div class="e-zeile">${neuVerbinden ? '<button class="knopf rahmen h50 wachsen" data-br="verbinden">Neu verbinden</button>' : ""}
      <button class="knopf primaer h50 wachsen" data-br="test" style="font-size:15px">Testdruck</button></div>
    <button class="knopf rahmen h50" data-br="freigeben">Für Phomemo-App freigeben (10 min)</button>`;
}
async function brAktion(was, knopf) {
  const basis = S.et && S.et.bruecke;
  if (!basis) return;
  if (knopf) knopf.disabled = true;
  // „Neu verbinden“ heißt nicht in jeder Version der Brücke gleich
  const pfade = was === "verbinden" ? ["/verbinden", "/neu_verbinden", "/neuverbinden", "/reconnect"] : ["/" + was];
  let text = "", ok = false;
  for (const p of pfade) {
    try {
      const r = await fetch(basis + p, { method: "POST" });
      if (r.status === 404) continue;
      const d = await r.json().catch(() => ({}));
      ok = r.ok && d.ok !== false;
      text = d.text || (ok ? (was === "test" ? "Testdruck geschickt" : "Erledigt") : `Druckbrücke meldet ${r.status}`);
      break;
    } catch (e) { text = "Druckbrücke nicht erreichbar"; break; }
  }
  meldung(text || "Diese Druckbrücke kennt das noch nicht", !ok);
  setTimeout(brLaden, 800);
}

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
  // Zeile 1–4 wie in SchichtLohn; ältere Auswahl („felder“) wird übernommen
  if (!Array.isArray(o.slots)) {
    const alt = o.felder.filter((k) => Etikett.FELDER.some(([x]) => x === k));
    o.slots = (alt.length ? alt : Etikett.STANDARD_SLOTS).slice(0, 4);
  }
  while (o.slots.length < 4) o.slots.push("keine");
  o.slots = o.slots.slice(0, 4);
  o.rgb = o.rgb === "aus" ? "aus" : "text";
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
  const da = (k) => k === "eigene" || k === "rest" || k === "id" || Etikett.hatWert(d.sorte, k, wo);
  const zeilen = o.slots.map((sl, i) => `<label class="et-zeile"><span class="ueber">Zeile ${i + 1}</span><select class="et-wahl" data-zeile="${i}">
      ${Etikett.FELDER.map(([k, n]) => `<option value="${k}"${k === sl ? " selected" : ""}>${esc(n)}${da(k) ? "" : " – nicht eingetragen"}</option>`).join("")}
      <option value="keine"${sl === "keine" ? " selected" : ""}>Keine</option></select></label>`).join("");
  const rgb = `<label class="et-zeile"><span class="ueber">RGB</span><select class="et-wahl" data-rgb="1">
      <option value="text"${o.rgb !== "aus" ? " selected" : ""}>Hex-Wert anzeigen</option><option value="aus"${o.rgb === "aus" ? " selected" : ""}>Aus</option></select></label>`;
  const eigene = o.slots.includes("eigene")
    ? `<input id="et-eigene" class="et-eigene" data-tastatur="frei" maxlength="40" placeholder="Eigener Text, z. B. Für Halloween-Deko" value="${esc(o.eigene)}">` : "";
  return `${aus}<div class="spalte gap6"><span class="ueber">Angaben auf dem Etikett</span><div class="et-zeilen">${zeilen}${rgb}</div>${eigene}<span id="et-weg" class="sub14 et-weg"></span></div>`;
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
  const erg = await Etikett.zeichnen($("et-canvas"), d.sorte, Object.assign(etZeichenOpt(d), { skala: 2 }));
  const weg = $("et-weg");
  if (weg) weg.textContent = erg && erg.weg && erg.weg.length ? `Passt nicht mehr drauf: ${erg.weg.join(", ")}` : "";
}
// QR-Code: mit eingetragener SchichtLohn-Adresse ein Link auf genau diese Rolle (SL-Nummer, sonst Strichcode) –
// sonst wie bisher die Lager-Liste in Home Assistant
function etQr(sorte) {
  const basis = String((S.et && S.et.qr) || "").trim();
  const code = sorte && (sorte.sl_id || (sorte.codes || [])[0]);
  if (!basis || !code) return S.et ? S.et.lager_url || "" : "";
  let b = /^https?:\/\//i.test(basis) ? basis : "https://" + basis;
  try { const u = new URL(b); if (u.pathname === "/" || !u.pathname) u.pathname = "/app/"; b = u.origin + u.pathname + u.search; } catch (e) { /* so lassen */ }
  return b + (b.includes("?") ? "&" : "?") + "rolle=" + encodeURIComponent(code);
}
// Optionen fürs Zeichnen – Vorschau mit skala 2, Druck in Originalauflösung
function etZeichenOpt(d) {
  return {
    format: d.format, rest: d.rest, qr: etQr(d.sorte),
    bildVersion: S.et && S.et.bilder ? S.et.bilder[d.sorte.hersteller] || 0 : 0,
    slots: d.opt.slots, rgb: d.opt.rgb, eigene: d.opt.eigene, hochkant: d.opt.hochkant,
    leerStandard: S.z ? S.z.leer_standard_g : 0,
  };
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
  const h = e.target.closest('[data-seg="et-hoch"]');
  if (h) d.opt.hochkant = h.dataset.v === "1";
  else return;
  etOptionenSpeichern(d.opt);
  etikettZeichnen();
};
$("et-optionen").onchange = (e) => {
  const d = S.etDialog;
  const w = e.target.closest("select.et-wahl");
  if (!d || !w) return;
  if (w.dataset.rgb) d.opt.rgb = w.value;
  else d.opt.slots[Number(w.dataset.zeile)] = w.value;
  etOptionenSpeichern(d.opt);
  etikettZeichnen();
};
$("et-zu").onclick = () => { $("etikett").classList.add("versteckt"); S.etDialog = null; };
$("et-druck").onclick = async () => {
  if (!S.etDialog) return;
  const cv = document.createElement("canvas");
  await Etikett.zeichnen(cv, S.etDialog.sorte, etZeichenOpt(S.etDialog));
  const r = Etikett.raster(cv);
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
    <div class="e-block"><span class="ueber">QR-Code auf dem Etikett</span>
      <span class="hinweis">Adresse von SchichtLohn, am besten über Tailscale (z. B. https://schichtlohn.dein-tailnet.ts.net). Dann öffnet der QR-Code mit der Handy-Kamera genau diese Rolle. Leer = Lager-Liste in Home Assistant.</span>
      <div class="e-zeile"><div class="feld wachsen"><input id="et-qr" data-tastatur="frei" inputmode="none" autocomplete="off" value="${esc(et.qr || "")}" placeholder="https://….ts.net" aria-label="Adresse für den QR-Code"></div>
        <button class="knopf primaer" style="height:42px;padding:0 16px;font-size:14px" data-et="qr">Speichern</button></div></div>
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
  if (d.et === "qr") { tastatur(false); await etSetzen({ qr: $("et-qr").value.trim() }); return meldung($("et-qr").value.trim() ? "QR-Adresse gespeichert" : "QR-Code zeigt wieder die Lager-Liste"); }
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

logoSetzen("#4caf50");  // bis die Einstellungen da sind
