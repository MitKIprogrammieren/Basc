// FilamentLager Waage – Etiketten für den Phomemo-Drucker.
// Zeichnet das Etikett in Schwarz-Weiß auf ein Canvas (Vorschau = Druck) und liefert das 1-Bit-Raster.
"use strict";

const Etikett = (() => {
  const PX_MM = 8; // 203 dpi
  const FORMATE = {
    classic: { name: "Classic", b: 40, h: 30 },
    expanded: { name: "Expanded", b: 50, h: 30 },
    compact: { name: "Compact", b: 40, h: 25 },
    vertikal: { name: "Vertikal", b: 30, h: 40 },
  };
  const KOPF_MM = 48;  // Druckkopf 384 Punkte bei 8 Punkten je mm
  // Bekanntes Format oder eigene Maße als "eigen-45x35"
  function formatVon(k) {
    if (FORMATE[k]) return FORMATE[k];
    const m = /^eigen-(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?)$/.exec(k || "");
    if (m) {
      const b = Number(m[1]), h = Number(m[2]);
      if (b >= 15 && b <= KOPF_MM && h >= 10 && h <= 100) return { name: "Eigene", b, h, eigen: true };
    }
    return null;
  }
  const SCHRIFT = "Montserrat, Arial, sans-serif";
  const bilder = new Map(); // Hersteller → { version, img }

  function bereich(a, b, e) {
    if (a < 0 && b < 0) return "";
    if (a < 0 || a === b) return `${b}${e}`;
    if (b < 0) return `${a}${e}`;
    return `${a}–${b}${e}`;
  }
  const komma = (v) => String(Math.round(v * 100) / 100);

  // Wählbare Angaben fürs Etikett, in Druckreihenfolge
  const FELDER = [
    ["duese", "Düse"], ["bett", "Bett"], ["trocknen", "Trocknen"], ["getrocknet", "Zuletzt getrocknet"],
    ["flow", "Flow / TD"], ["netto", "Nettogewicht"], ["leer", "Leergewicht"], ["ort", "Lagerort"],
    ["eigene", "Eigene Zeile"], ["rest", "Rest"],
  ];
  const STANDARD_FELDER = ["duese", "bett", "trocknen", "flow"];  // Rest nur auf Wunsch – er veraltet

  // Übliche Trocknungswerte, wenn im Lager nichts eingetragen ist (Richtwerte, mit „~“ gedruckt)
  const TROCKNEN_RICHTWERTE = [
    [/TPU|TPE/, 55, 8], [/PETG|PET|PCTG/, 65, 8], [/ASA|ABS/, 80, 8], [/PC\b|PC-|POLYCARB/, 80, 8],
    [/PA|NYLON|PAHT|PPA/, 80, 12], [/PVA|BVOH/, 50, 8], [/PLA/, 55, 8],
  ];
  function trocknen(s) {
    if (s.trocknen_c > 0) return { c: s.trocknen_c, h: s.trocknen_h, richtwert: false };
    const m = String(s.material || "").toUpperCase();
    const t = TROCKNEN_RICHTWERTE.find(([re]) => re.test(m));
    return t ? { c: t[1], h: t[2], richtwert: true } : null;
  }
  function datum(iso) {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso || "");
    return m ? `${m[3]}.${m[2]}.${m[1].slice(2)}` : "";
  }

  // Eine Zeile [Beschriftung, Wert] für ein Feld – oder null, wenn die Sorte dafür nichts hat
  function feldZeile(s, f, o = {}) {
    if (f === "duese") { const v = bereich(s.duese_min, s.duese_max, " °C"); return v ? ["Düse", v] : null; }
    if (f === "bett") { const v = bereich(s.bett_min ?? -1, s.bett_max ?? -1, " °C"); return v ? ["Bett", v] : null; }
    if (f === "trocknen") {
      const t = trocknen(s);
      return t ? ["Trocknen", `${t.richtwert ? "~" : ""}${t.c} °C${t.h > 0 ? ` · ${t.h} h` : ""}`] : null;
    }
    if (f === "getrocknet") { const d = datum(s.getrocknet); return d ? ["Getrockn.", d] : null; }
    if (f === "flow") {
      if (s.flow > 0 && s.td > 0) return ["Flow / TD", `${komma(s.flow)} / ${komma(s.td)}`];
      if (s.flow > 0) return ["Flow", komma(s.flow)];
      if (s.td > 0) return ["TD", komma(s.td)];
      return null;
    }
    if (f === "netto") return s.gewicht > 0 ? ["Netto", `${s.gewicht} g`] : null;
    if (f === "leer") { const l = s.leer_g > 0 ? s.leer_g : o.leerStandard || 0; return l > 0 ? ["Leer", `${l} g`] : null; }
    if (f === "ort") return s.ort ? ["Ort", s.ort] : null;
    if (f === "eigene") { const t = String(o.eigene || "").trim(); return t ? ["", t] : null; }
    if (f === "rest") return (o.rest || 0) > 0 ? ["Rest", `${o.rest} g`] : null;
    return null;
  }
  const hatWert = (s, f, o) => !!feldZeile(s, f, o);

  // Zeilen unten links; "Rest" bleibt immer drin, wenn er gewählt ist und es einen gibt
  function zeilen(s, o, max) {
    const felder = o.felder || STANDARD_FELDER;
    const rest = o.rest || 0;
    const z = [];
    let restZeile = null;
    for (const f of felder) {
      if (f === "rest") { if (rest > 0) restZeile = ["Rest", `${rest} g`]; continue; }
      const zeile = feldZeile(s, f, o);
      if (zeile) z.push(zeile);
    }
    if (!z.length && !restZeile) z.push(["Ø", `${komma(s.durchmesser || 1.75)} mm · ${s.gewicht} g`]);
    const platz = Math.max(restZeile ? 0 : 1, max - (restZeile ? 1 : 0));
    const out = z.slice(0, platz);
    const lang = { "Getrockn.": "Zuletzt getrocknet", Ort: "Lagerort", Leer: "Leergewicht", Netto: "Nettogewicht" };
    out.weg = z.slice(platz).map(([n]) => (n ? lang[n] || n : "Eigene Zeile"));
    if (restZeile) out.push(restZeile);
    return out;
  }

  // größte Schrift, mit der der Text in die Breite passt; notfalls mit „…“ gekürzt
  function passend(ctx, text, maxB, groesse, gewicht, min = 10) {
    let g = groesse;
    for (; g > min; g--) {
      ctx.font = `${gewicht} ${g}px ${SCHRIFT}`;
      if (ctx.measureText(text).width <= maxB) return { text, g };
    }
    ctx.font = `${gewicht} ${min}px ${SCHRIFT}`;
    let t = text;
    while (t.length > 1 && ctx.measureText(t + "…").width > maxB) t = t.slice(0, -1);
    return { text: t === text ? t : t + "…", g: min };
  }

  function text(ctx, t, x, y, g, gewicht, farbe = "#000", ausrichtung = "left") {
    ctx.font = `${gewicht} ${g}px ${SCHRIFT}`;
    ctx.fillStyle = farbe;
    ctx.textAlign = ausrichtung;
    ctx.textBaseline = "alphabetic";
    ctx.fillText(t, x, y);
  }

  function qr(ctx, inhalt, x, y, groesse, mitte = false) {
    if (!inhalt || !window.QRCode || groesse < 40) return;
    const halter = document.createElement("div");
    let modell;
    try {
      const q = new QRCode(halter, { text: inhalt, width: 64, height: 64, correctLevel: QRCode.CorrectLevel.L });
      modell = q._oQRCode;
    } catch (e) { return; }
    if (!modell) return;
    const n = modell.getModuleCount();
    const m = Math.floor(groesse / n);
    if (m < 2) return;
    const ox = x + (mitte ? Math.floor((groesse - n * m) / 2) : groesse - n * m), oy = y + groesse - n * m;
    ctx.fillStyle = "#000";
    for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (modell.isDark(r, c)) ctx.fillRect(ox + c * m, oy + r * m, m, m);
  }

  async function bild(hersteller, version) {
    if (!hersteller || !version) return null;
    const alt = bilder.get(hersteller);
    if (alt && alt.version === version) return alt.img;
    const img = new Image();
    img.src = `/api/hersteller_bild?name=${encodeURIComponent(hersteller)}&v=${version}`;
    try { await img.decode(); } catch (e) { return null; }
    bilder.set(hersteller, { version, img });
    return img;
  }

  // Kopfzeile: hinterlegtes Bild des Herstellers, sonst der Name in fetter Schrift
  function kopf(ctx, s, img, x, y, b, h) {
    if (img) {
      const f = Math.min(b / img.naturalWidth, h / img.naturalHeight);
      const w = Math.round(img.naturalWidth * f), hh = Math.round(img.naturalHeight * f);
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(img, x, y + Math.round((h - hh) / 2), w, hh);
      return;
    }
    const name = s.hersteller || "Filament";
    const p = passend(ctx, name, b, Math.round(h * 1.05), 800, 12);
    text(ctx, p.text, x, y + Math.round(h * 0.86), p.g, 800);
  }

  /** Zeichnet das Etikett. o: { format, rest, qr (Inhalt), bildVersion, felder, eigene, leerStandard, hochkant }
   *  hochkant: auf derselben Rolle um 90° gedreht – die Vorschau zeigt es aufrecht, raster() dreht fürs Drucken. */
  async function zeichnen(canvas, s, o = {}) {
    const f = formatVon(o.format) || FORMATE.expanded;
    const drehen = !!o.hochkant && f.b > f.h;
    const W = Math.round((drehen ? f.h : f.b) * PX_MM), H = Math.round((drehen ? f.b : f.h) * PX_MM);
    canvas.width = W; canvas.height = H;
    canvas.dataset.drehen = drehen ? "1" : "";
    await Promise.all([document.fonts.load(`800 20px Montserrat`), document.fonts.load(`700 20px Montserrat`),
      document.fonts.load(`500 20px Montserrat`)]).catch(() => {});
    const img = await bild(s.hersteller, o.bildVersion);
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, W, H);
    const hoch = H > W;
    const ref = hoch ? W : H;
    const m = Math.round(ref * 0.055);
    const innen = W - 2 * m;
    let y = m;

    // Hersteller
    const kh = Math.round(ref * 0.16);
    kopf(ctx, s, img, m, y, innen, kh);
    y += kh + Math.round(ref * 0.02);

    // Material als schwarzer Balken, daneben der Farbcode
    const bh = Math.round(ref * 0.17);
    const material = [s.material, (s.besonderheiten || [])[0]].filter(Boolean).join(" ") || s.summary;
    const hex = (s.hex || "").toUpperCase();
    ctx.font = `700 ${Math.round(bh * 0.5)}px ${SCHRIFT}`;
    const hexB = hoch ? 0 : ctx.measureText(hex).width + Math.round(ref * 0.03);
    const balkenB = innen - hexB;
    ctx.fillStyle = "#000";
    ctx.fillRect(m, y, balkenB, bh);
    const mp = passend(ctx, material, balkenB - Math.round(bh * 0.3), Math.round(bh * 0.82), 800, 12);
    text(ctx, mp.text, m + Math.round(bh * 0.15), y + Math.round(bh * 0.5 + mp.g * 0.36), mp.g, 800, "#fff");
    if (!hoch) text(ctx, hex, W - m, y + Math.round(bh * 0.5 + bh * 0.18), Math.round(bh * 0.5), 700, "#000", "right");
    y += bh + Math.round(ref * 0.02);

    // Farbe (mit Farbcode des Herstellers)
    const farbe = [s.farbe, s.fcode ? `(${s.fcode})` : ""].filter(Boolean).join(" ");
    const fp = passend(ctx, farbe, innen, Math.round(ref * 0.12), 700, 11);
    y += Math.round(fp.g * 1.0);
    text(ctx, fp.text, m, y, fp.g, 700);
    if (hoch && hex) { y += Math.round(ref * 0.1); text(ctx, hex, m, y, Math.round(ref * 0.08), 700); }
    y += Math.round(ref * 0.05);

    // unten: Werte und QR-Code – quer nebeneinander, hochkant untereinander
    const unten = H - m;
    // Zeilenhöhe: groß, solange Platz ist – bei vielen Werten etwas enger
    const voll = zeilen(s, o, 6).length;
    const zh = Math.round(Math.max(ref * 0.072, Math.min(ref * 0.095, (unten - y) / voll)));
    const zg = Math.round(zh * 0.82);
    const werte = (liste, x, yy, breite) => {
      ctx.font = `600 ${zg}px ${SCHRIFT}`;
      const spalte = Math.max(0, ...liste.filter(([n]) => n).map(([n]) => ctx.measureText(n).width)) + Math.round(ref * 0.04);
      liste.forEach(([n, w], i) => {
        const zy = yy + (i + 1) * zh - Math.round(zh * 0.2);
        if (!n) {  // eigene Zeile: ganze Breite
          const ep = passend(ctx, w, breite, zg, 700, 10);
          text(ctx, ep.text, x, zy, ep.g, 700);
          return;
        }
        text(ctx, n, x, zy, zg, 600);
        const wp = passend(ctx, w, breite - spalte, zg, n === "Rest" ? 800 : 700, 10);
        text(ctx, wp.text, x + spalte, zy, wp.g, n === "Rest" ? 800 : 700);
      });
    };
    let weg = [];
    if (hoch) {
      // Hochkant: Werte oben, QR-Code unten rechts – passen nicht alle über den Code, kommen die übrigen links daneben
      const luecke = Math.round(ref * 0.04);
      const q0 = o.qr ? Math.round(W * 0.4) : 0;
      const oben = Math.max(0, Math.floor((unten - y - (q0 ? q0 + luecke : 0)) / zh));
      const daneben = q0 ? Math.floor(q0 / zh) : 0;
      const liste = zeilen(s, o, Math.max(1, Math.min(8, oben + daneben)));
      weg = liste.weg;
      const obenListe = liste.slice(0, oben), nebenListe = liste.slice(oben);
      werte(obenListe, m, y, innen);
      y += obenListe.length * zh + luecke;
      if (o.qr) {
        if (nebenListe.length) {
          werte(nebenListe, m, unten - q0, innen - q0 - luecke);
          if (q0 >= 48) qr(ctx, o.qr, W - m - q0, unten - q0, q0);
        } else {
          // Platz übrig: Code größer und mittig statt einsam in der Ecke
          const q = Math.min(innen, unten - y);
          if (q >= 48) qr(ctx, o.qr, Math.round((W - q) / 2), unten - q, q, true);
        }
      }
    } else {
      const q = o.qr ? Math.min(unten - y, Math.round(W * 0.3)) : 0;
      const liste = zeilen(s, o, Math.max(1, Math.min(6, Math.floor((unten - y) / zh))));
      werte(liste, m, unten - liste.length * zh, innen - q - Math.round(ref * 0.03));
      weg = liste.weg;
      if (q) qr(ctx, o.qr, W - m - q, unten - q, q);
    }

    schwarzWeiss(ctx, W, H);
    return { breite: W, hoehe: H, weg };
  }

  // Thermodruck kennt nur Schwarz und Weiß – die Vorschau zeigt genau das
  function schwarzWeiss(ctx, W, H) {
    const d = ctx.getImageData(0, 0, W, H);
    const p = d.data;
    for (let i = 0; i < p.length; i += 4) {
      const a = p[i + 3] / 255;
      const l = (0.299 * p[i] + 0.587 * p[i + 1] + 0.114 * p[i + 2]) * a + 255 * (1 - a);
      const v = l < 140 ? 0 : 255;
      p[i] = p[i + 1] = p[i + 2] = v; p[i + 3] = 255;
    }
    ctx.putImageData(d, 0, 0);
  }

  /** 1-Bit-Raster für den Drucker: 1 = schwarz, Breite auf 8 Punkte aufgerundet, Base64. */
  function raster(canvas) {
    if (canvas.dataset.drehen === "1") {  // hochkant: 90° im Uhrzeigersinn auf die Rolle drehen
      const g = document.createElement("canvas");
      g.width = canvas.height; g.height = canvas.width;
      const c = g.getContext("2d");
      c.fillStyle = "#fff"; c.fillRect(0, 0, g.width, g.height);
      c.translate(g.width, 0); c.rotate(Math.PI / 2);
      c.drawImage(canvas, 0, 0);
      canvas = g;
    }
    const W = canvas.width, H = canvas.height;
    const bpr = Math.ceil(W / 8);
    const p = canvas.getContext("2d").getImageData(0, 0, W, H).data;
    const out = new Uint8Array(bpr * H);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      if (p[(y * W + x) * 4] < 128) out[y * bpr + (x >> 3)] |= 0x80 >> (x & 7);
    }
    let bin = "";
    for (let i = 0; i < out.length; i += 0x8000) bin += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
    return { breite: bpr * 8, hoehe: H, daten: btoa(bin) };
  }

  // Beispiel für den Testdruck
  const TEST = { hersteller: "FilamentLager", material: "PLA", besonderheiten: ["Test"], farbe: "Testetikett",
    hex: "#4CAF50", fcode: "", duese_min: 190, duese_max: 230, bett_min: 55, bett_max: 65, flow: 0.98, td: -1,
    trocknen_c: -1, trocknen_h: -1, durchmesser: 1.75, gewicht: 1000, leer_g: 250, ort: "Regal 1",
    getrocknet: new Date().toISOString().slice(0, 10) };

  return { FORMATE, KOPF_MM, formatVon, hatWert, FELDER, STANDARD_FELDER, trocknen, datum, zeichnen, raster, TEST };
})();
