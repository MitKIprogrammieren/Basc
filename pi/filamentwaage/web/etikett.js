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
  const SCHRIFT = "Montserrat, Arial, sans-serif";
  const bilder = new Map(); // Hersteller → { version, img }

  function bereich(a, b, e) {
    if (a < 0 && b < 0) return "";
    if (a < 0 || a === b) return `${b}${e}`;
    if (b < 0) return `${a}${e}`;
    return `${a}–${b}${e}`;
  }
  const komma = (v) => String(Math.round(v * 100) / 100);

  // Zeilen unten links; "Rest" bleibt immer drin, wenn es einen gibt
  function zeilen(s, rest, max) {
    const z = [];
    const duese = bereich(s.duese_min, s.duese_max, " °C");
    if (duese) z.push(["Düse", duese]);
    const bett = bereich(s.bett_min ?? -1, s.bett_max ?? -1, " °C");
    if (bett) z.push(["Bett", bett]);
    if (s.trocknen_c > 0) z.push(["Trocknen", `${s.trocknen_c} °C${s.trocknen_h > 0 ? ` · ${s.trocknen_h} h` : ""}`]);
    if (s.flow > 0 && s.td > 0) z.push(["Flow / TD", `${komma(s.flow)} / ${komma(s.td)}`]);
    else if (s.flow > 0) z.push(["Flow", komma(s.flow)]);
    else if (s.td > 0) z.push(["TD", komma(s.td)]);
    if (!z.length) z.push(["Ø", `${komma(s.durchmesser || 1.75)} mm · ${s.gewicht} g`]);
    const restZeile = rest > 0 ? ["Rest", `${rest} g`] : null;
    const platz = Math.max(1, max - (restZeile ? 1 : 0));
    const out = z.slice(0, platz);
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

  function qr(ctx, inhalt, x, y, groesse) {
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
    const ox = x + groesse - n * m, oy = y + groesse - n * m;
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

  /** Zeichnet das Etikett. o: { format, rest, qr (Inhalt), bildVersion } */
  async function zeichnen(canvas, s, o = {}) {
    const f = FORMATE[o.format] || FORMATE.expanded;
    const W = f.b * PX_MM, H = f.h * PX_MM;
    canvas.width = W; canvas.height = H;
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
    const voll = zeilen(s, o.rest || 0, 5).length;
    const zh = Math.round(Math.max(ref * 0.072, Math.min(ref * 0.095, (unten - y) / voll)));
    const zg = Math.round(zh * 0.82);
    const werte = (liste, x, yy, breite) => {
      ctx.font = `600 ${zg}px ${SCHRIFT}`;
      const spalte = Math.max(...liste.map(([n]) => ctx.measureText(n).width)) + Math.round(ref * 0.04);
      liste.forEach(([n, w], i) => {
        const zy = yy + (i + 1) * zh - Math.round(zh * 0.2);
        text(ctx, n, x, zy, zg, 600);
        const wp = passend(ctx, w, breite - spalte, zg, n === "Rest" ? 800 : 700, 10);
        text(ctx, wp.text, x + spalte, zy, wp.g, n === "Rest" ? 800 : 700);
      });
    };
    if (hoch) {
      const qMin = o.qr ? Math.round(W * 0.32) : 0;
      const liste = zeilen(s, o.rest || 0, Math.max(1, Math.min(3, Math.floor((unten - y - qMin) / zh))));
      werte(liste, m, y, innen);
      y += liste.length * zh + Math.round(ref * 0.03);
      const q = o.qr ? Math.min(unten - y, Math.round(W * 0.45)) : 0;
      if (q >= 48) qr(ctx, o.qr, W - m - q, unten - q, q);
    } else {
      const q = o.qr ? Math.min(unten - y, Math.round(W * 0.3)) : 0;
      const liste = zeilen(s, o.rest || 0, Math.max(1, Math.min(5, Math.floor((unten - y) / zh))));
      werte(liste, m, unten - liste.length * zh, innen - q - Math.round(ref * 0.03));
      if (q) qr(ctx, o.qr, W - m - q, unten - q, q);
    }

    schwarzWeiss(ctx, W, H);
    return { breite: W, hoehe: H };
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
    trocknen_c: -1, trocknen_h: -1, durchmesser: 1.75, gewicht: 1000 };

  return { FORMATE, zeichnen, raster, TEST };
})();
