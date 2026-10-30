// FilamentLager Waage – Etiketten für den Phomemo-Drucker. Gleiche Formate, Zeilen und Zeichnung wie in SchichtLohn.
// Zeichnet das Etikett in Schwarz-Weiß auf ein Canvas und liefert das 1-Bit-Raster. Vorschau mit skala 2 (scharf), Druck mit skala 1.
"use strict";

const Etikett=(()=>{
  "use strict";
  const PX_MM=8; // 203 dpi – Druck. Vorschau/Bild mit o.skala (z. B. 4) gestochen scharf; o.dpi=300 zeichnet direkt für 300-dpi-Drucker (M20 Pro)
  let K=1;
  /* art: welche Zeichnung. werte = wie viele Slots das Format zeigen kann */
  const FORMATE={
    classic:{name:"Classic",b:40,h:30,art:"quer",werte:4},
    expanded:{name:"Expanded",b:50,h:30,art:"quer",werte:4},
    compact:{name:"Compact",b:40,h:25,art:"kompakt",werte:0},
    slim:{name:"Slim",b:40,h:12,art:"slim",werte:0},
    slicer:{name:"Für Slicer",b:40,h:30,art:"slicer",werte:4},
    vertikal:{name:"Vertikal",b:30,h:40,art:"hoch",werte:4},
    vertikal_fett:{name:"Vertikal fett",b:30,h:40,art:"hochfett",werte:2},
    vertikal_kompakt:{name:"Vertikal kompakt",b:25,h:40,art:"hochkompakt",werte:2}};
  const KOPF_MM=48;
  function formatVon(k){if(FORMATE[k])return FORMATE[k];const m=/^eigen-(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?)$/.exec(k||"");
    if(m){const b=Number(m[1]),h=Number(m[2]);if(b>=15&&b<=KOPF_MM&&h>=10&&h<=100)return {name:"Eigene",b,h,eigen:true,art:h>b?"hoch":"quer",werte:4}}return null}
  const SCHRIFT="Montserrat, Arial, sans-serif";
  const bilder=new Map();

  function bereich(a,b,e){if(a<0&&b<0)return "";if(a<0||a===b)return `${b}${e}`;if(b<0)return `${a}${e}`;return `${a}–${b}${e}`}
  const komma=v=>String(Math.round(v*1000)/1000);
  const n=v=>typeof v==="number"&&v>0;
  /* Alle wählbaren Angaben – Reihenfolge wie in der Auswahl. [Schlüssel, Name in der Auswahl] */
  const FELDER=[["duese","Düse"],["bett","Bett"],["kammer","Kammer"],
    ["trocknen","Trocknen (°C & h)"],["trocknen_c","Trocknen °C"],["trocknen_h","Trockenzeit"],["getrocknet","Zuletzt getrocknet"],
    ["flow","Flow"],["flowinfo","Flow-Info (Vol. & Flow)"],["mvs","Max. Volumenstrom"],["k","K-Wert"],
    ["buegeln","Bügeln (Tempo & Flow)"],["buegeln_flow","Bügel-Flow"],["buegeln_tempo","Bügel-Tempo"],
    ["schwund","Schwund"],["td","TD"],["ams","AMS-tauglich"],["ams_modelle","AMS-Modelle"],
    ["sku","Artikelnummer"],["id","Filament-ID"],["netto","Nettogewicht"],["leer","Leergewicht"],["rest","Restgewicht"],["ort","Lagerort"],["eigene","Eigene Zeile"]];
  const STANDARD_SLOTS=["duese","bett","flow","flowinfo"];
  const STANDARD_FELDER=STANDARD_SLOTS; // ältere Aufrufer
  const TROCKNEN_RICHTWERTE=[[/TPU|TPE/,55,8],[/PETG|PET|PCTG/,65,8],[/ASA|ABS/,80,8],[/PC\b|PC-|POLYCARB/,80,8],[/PA|NYLON|PAHT|PPA/,80,12],[/PVA|BVOH/,50,8],[/PLA/,55,8]];
  function trocknen(s){if(s.trocknen_c>0)return {c:s.trocknen_c,h:s.trocknen_h,richtwert:false};const m=String(s.material||"").toUpperCase();const t=TROCKNEN_RICHTWERTE.find(([re])=>re.test(m));return t?{c:t[1],h:t[2],richtwert:true}:null}
  function datum(iso){const m=/^(\d{4})-(\d{2})-(\d{2})$/.exec(iso||"");return m?`${m[3]}.${m[2]}.${m[1].slice(2)}`:""}
  /* [Kurzname auf dem Etikett, Wert] oder null, wenn für diese Sorte nichts hinterlegt ist */
  function feldZeile(s,f,o={}){
    switch(f){
      case "duese":{const v=bereich(s.duese_min,s.duese_max," °C");return v?["Düse",v]:null}
      case "bett":{const v=bereich(s.bett_min??-1,s.bett_max??-1," °C");return v?["Bett",v]:null}
      case "kammer":return n(s.kammer)?["Kammer",`${s.kammer} °C`]:null;
      case "trocknen":{const t=trocknen(s);return t?["Trocknen",`${t.richtwert?"~":""}${t.c} °C${t.h>0?` · ${t.h} h`:""}`]:null}
      case "trocknen_c":{const t=trocknen(s);return t?["Trocknen",`${t.richtwert?"~":""}${t.c} °C`]:null}
      case "trocknen_h":{const t=trocknen(s);return t&&t.h>0?["Trockenzeit",`${t.richtwert?"~":""}${t.h} h`]:null}
      case "getrocknet":{const d=datum(s.getrocknet);return d?["Getrockn.",d]:null}
      case "flow":return n(s.flow)?["Flow",komma(s.flow)]:null;
      case "flowinfo":{if(n(s.mvs)&&n(s.flow))return ["Flow-Info",`${komma(s.mvs)} mm³/s · ${komma(s.flow)}`];if(n(s.mvs))return ["Max. Vol.",`${komma(s.mvs)} mm³/s`];return null}
      case "mvs":return n(s.mvs)?["Max. Vol.",`${komma(s.mvs)} mm³/s`]:null;
      case "k":return n(s.k)?["K-Wert",komma(s.k)]:null;
      case "buegeln":{if(n(s.buegeln_tempo)&&n(s.buegeln_flow))return ["Bügeln",`${s.buegeln_tempo} mm/s · ${s.buegeln_flow} %`];return null}
      case "buegeln_flow":return n(s.buegeln_flow)?["Bügel-Flow",`${s.buegeln_flow} %`]:null;
      case "buegeln_tempo":return n(s.buegeln_tempo)?["Bügel-Tempo",`${s.buegeln_tempo} mm/s`]:null;
      case "schwund":return n(s.schwund)?["Schwund",`${komma(s.schwund)} %`]:null;
      case "td":return n(s.td)?["TD",komma(s.td)]:null;
      case "ams":return s.ams===true?["AMS","geeignet"]:s.ams===false?["AMS","nicht geeignet"]:null;
      case "ams_modelle":return s.ams_modelle?["AMS",s.ams_modelle]:null;
      case "sku":return s.sku?["Art.-Nr.",s.sku]:null;
      case "id":return o.id?["ID",o.id]:null;
      case "netto":return s.gewicht>0?["Netto",`${s.gewicht} g`]:null;
      case "leer":{const l=s.leer_g>0?s.leer_g:o.leerStandard||0;return l>0?["Leer",`${l} g`]:null}
      case "rest":return (o.rest||0)>0?["Rest",`${o.rest} g`]:null;
      case "ort":return s.ort?["Ort",s.ort]:null;
      case "eigene":{const t=String(o.eigene||"").trim();return t?["",t]:null}
    }
    return null}
  const hatWert=(s,f,o)=>!!feldZeile(s,f,o);
  /* Slots 1–4 (oder ältere „felder“) → Zeilen; leere Slots und fehlende Werte fallen weg */
  function slotsVon(o){const sl=Array.isArray(o.slots)?o.slots:(o.felder||STANDARD_SLOTS);return sl.filter(f=>f&&f!=="keine")}
  function zeilen(s,o,max){const z=[];for(const f of slotsVon(o)){const zeile=feldZeile(s,f,o);if(zeile)z.push(zeile)}
    const out=z.slice(0,Math.max(0,max));out.weg=z.slice(Math.max(0,max)).map(([nm])=>nm||"Eigene Zeile");return out}
  function passend(ctx,text,maxB,groesse,gewicht,min=10){min=Math.round(min*K);let g=groesse;for(;g>min;g--){ctx.font=`${gewicht} ${g}px ${SCHRIFT}`;if(ctx.measureText(text).width<=maxB)return {text,g}}
    ctx.font=`${gewicht} ${min}px ${SCHRIFT}`;let t=text;while(t.length>1&&ctx.measureText(t+"…").width>maxB)t=t.slice(0,-1);return {text:t===text?t:t+"…",g:min}}
  function umbrechen(ctx,text,maxB,g,gewicht,maxZeilen){ctx.font=`${gewicht} ${g}px ${SCHRIFT}`;const w=String(text).split(/\s+/);const z=[];let akt="";
    for(const x of w){const t=akt?akt+" "+x:x;if(ctx.measureText(t).width<=maxB||!akt)akt=t;else{z.push(akt);akt=x}}if(akt)z.push(akt);
    if(z.length>maxZeilen){const r=z.slice(0,maxZeilen);r[maxZeilen-1]=passend(ctx,z.slice(maxZeilen-1).join(" "),maxB,g,gewicht,g).text;return r}return z}
  function text(ctx,t,x,y,g,gewicht,farbe="#000",ausrichtung="left"){ctx.font=`${gewicht} ${g}px ${SCHRIFT}`;ctx.fillStyle=farbe;ctx.textAlign=ausrichtung;ctx.textBaseline="alphabetic";ctx.fillText(t,x,y)}
  function qr(ctx,inhalt,x,y,groesse,ausrichtung="unten-rechts"){if(!inhalt||(!window.qrcode&&!window.QRCode)||groesse<40*K)return 0;let modell;
    try{if(window.qrcode){modell=window.qrcode(0,"L");modell.addData(inhalt);modell.make()}
      else{const q=new QRCode(document.createElement("div"),{text:inhalt,width:64,height:64,correctLevel:QRCode.CorrectLevel.L});modell=q._oQRCode}}catch(e){return 0}
    if(!modell)return 0;
    const nm=modell.getModuleCount();const m=Math.floor(groesse/nm);if(m<2*K)return 0;const g=nm*m;
    const ox=ausrichtung.includes("links")?x:ausrichtung.includes("mitte")?x+Math.floor((groesse-g)/2):x+groesse-g;
    const oy=ausrichtung.includes("oben")?y:y+groesse-g;ctx.fillStyle="#000";
    for(let r=0;r<nm;r++)for(let c=0;c<nm;c++)if(modell.isDark(r,c))ctx.fillRect(ox+c*m,oy+r*m,m,m);return g}
  /* Bild, das du pro Hersteller in der Waage hinterlegst (/api/hersteller_bild) */
  async function bild(hersteller,version){if(!hersteller||!version)return null;const alt=bilder.get(hersteller);if(alt&&alt.version===version)return alt.img;
    const img=new Image();img.src=`/api/hersteller_bild?name=${encodeURIComponent(hersteller)}&v=${version}`;try{await img.decode()}catch(e){return null}
    const druck=fuerDruck(img)||img;bilder.set(hersteller,{version,img:druck});return druck}
  function fuerDruck(img){try{const c=document.createElement("canvas");c.width=img.naturalWidth;c.height=img.naturalHeight;if(!c.width||!c.height)return null;const x=c.getContext("2d");x.drawImage(img,0,0);
      const d=x.getImageData(0,0,c.width,c.height),p=d.data;let sum=0,nn=0,durch=0;
      for(let i=0;i<p.length;i+=4){if(p[i+3]>128){sum+=Math.min(p[i],p[i+1],p[i+2]);nn++}else durch++}
      const hellAufDurchsichtig=nn>0&&durch>nn*0.2&&sum/nn>200;
      for(let i=0;i<p.length;i+=4){const r=p[i],g=p[i+1],b=p[i+2],a=p[i+3];const hell=Math.min(r,g,b),bunt=Math.max(r,g,b)-hell;
        const tinte=hellAufDurchsichtig?a/255:(a/255)*Math.max((255-hell)/255,bunt/120>1?1:bunt/120);const v=tinte>0.3?0:255;p[i]=p[i+1]=p[i+2]=v;p[i+3]=255}
      x.putImageData(d,0,0);return c}catch(e){return null}}
  function kopf(ctx,s,img,x,y,b,h){if(img){const iw=img.naturalWidth||img.width,ih=img.naturalHeight||img.height;const f=Math.min(b/iw,h/ih);const w=Math.round(iw*f),hh=Math.round(ih*f);
      ctx.imageSmoothingEnabled=true;ctx.drawImage(img,x,y+Math.round((h-hh)/2),w,hh);return}
    const name=s.hersteller||"Filament";const p=passend(ctx,name,b,Math.round(h*1.05),800,10);text(ctx,p.text,x,y+Math.round(h*0.86),p.g,800)}
  const materialVon=s=>[s.material,(s.besonderheiten||[])[0]].filter(Boolean).join(" ")||s.summary||"";
  const farbeVon=s=>[s.farbe,s.fcode?`(${s.fcode})`:""].filter(Boolean).join(" ");
  /* schwarzer Balken mit weißem Material */
  function balken(ctx,s,x,y,b,h){ctx.fillStyle="#000";ctx.fillRect(x,y,b,h);const mp=passend(ctx,materialVon(s),b-Math.round(h*0.3),Math.round(h*0.82),800,9);
    text(ctx,mp.text,x+Math.round(h*0.15),y+Math.round(h*0.5+mp.g*0.36),mp.g,800,"#fff")}
  /* Werteliste: Name links, Wert in eigener Spalte */
  function werte(ctx,liste,x,y,breite,zh,ref,fett=false){
    // Name schlicht und etwas kleiner, Wert fett – alle Werte gleich groß, damit die Spalte ruhig wirkt
    const gn=Math.round(zh*0.62),gw0=Math.round(zh*0.84);ctx.font=`500 ${gn}px ${SCHRIFT}`;
    const spalte=Math.max(0,...liste.filter(([nm])=>nm).map(([nm])=>ctx.measureText(nm).width))+Math.round(ref*0.04);
    const gw=Math.min(gw0,...liste.map(([nm,w])=>passend(ctx,w,breite-(nm?spalte:0),gw0,800,8).g));
    liste.forEach(([nm,w],i)=>{const zy=y+(i+1)*zh-Math.round(zh*0.22);if(nm)text(ctx,nm,x,zy,gn,500);
      const wp=passend(ctx,w,breite-(nm?spalte:0),gw,800,8);text(ctx,wp.text,x+(nm?spalte:0),zy,wp.g,800)})}
  /* Werteliste untereinander: kleiner Name, darunter großer Wert (schmale Etiketten) */
  function werteGestapelt(ctx,liste,x,y,breite,zh,gw){liste.forEach(([nm,w],i)=>{const y0=y+i*zh;const g1=Math.round(zh*0.3),g2=gw||Math.round(zh*0.5);
      if(nm)text(ctx,nm,x,y0+g1,g1,500);const wp=passend(ctx,w,breite,g2,800,9);text(ctx,wp.text,x,y0+g1+Math.round(zh*0.08)+wp.g,wp.g,800)})}

  /** o: { format, slots[4], rgb ("text"|"aus"), qr (Inhalt), id, rest, eigene, leerStandard, hochkant } */
  async function zeichnen(canvas,s,o={}){
    const f=formatVon(o.format)||FORMATE.classic;const drehen=!!o.hochkant&&f.b>f.h;const dpi=o.dpi===300?300:203;K=Math.max(1,Math.min(6,Math.round(o.skala||1)))*(dpi===300?300/203.2:1);
    const W=Math.round((drehen?f.h:f.b)*PX_MM*K),H=Math.round((drehen?f.b:f.h)*PX_MM*K);canvas.width=W;canvas.height=H;canvas.dataset.drehen=drehen?"1":"";canvas.dataset.dpi=String(dpi);
    await Promise.all([800,700,600,500].map(w=>document.fonts.load(`${w} 20px Montserrat`))).catch(()=>{});
    const img=await bild(s.hersteller,o.bildVersion).catch(()=>null);
    const ctx=canvas.getContext("2d",{willReadFrequently:true});ctx.fillStyle="#fff";ctx.fillRect(0,0,W,H);
    const B=W,Hh=H;const hex=o.rgb==="aus"?"":(s.hex||"").toUpperCase();
    const art=drehen?"hoch":(f.art||"quer");let weg=[];
    const zeichner={quer,kompakt,slim,slicer,hoch,hochfett,hochkompakt}[art]||quer;
    weg=zeichner(ctx,s,o,img,B,Hh,hex,f)||[];
    schwarzWeiss(ctx,W,H);return {breite:W,hoehe:H,weg}}

  /* Classic / Expanded: Logo, Balken + Hex, Farbe, Werte links, QR rechts unten */
  function quer(ctx,s,o,img,W,H,hex,f){const ref=H;const m=Math.round(ref*0.055);const innen=W-2*m;let y=m;
    const kh=Math.round(ref*0.16);kopf(ctx,s,img,m,y,innen,kh);y+=kh+Math.round(ref*0.02);
    const bh=Math.round(ref*0.17);ctx.font=`700 ${Math.round(bh*0.5)}px ${SCHRIFT}`;const hexB=hex?ctx.measureText(hex).width+Math.round(ref*0.03):0;
    balken(ctx,s,m,y,innen-hexB,bh);if(hex)text(ctx,hex,W-m,y+Math.round(bh*0.68),Math.round(bh*0.5),700,"#000","right");y+=bh+Math.round(ref*0.02);
    const fp=passend(ctx,farbeVon(s),innen,Math.round(ref*0.12),700,10);y+=fp.g;text(ctx,fp.text,m,y,fp.g,700);y+=Math.round(ref*0.04);
    const unten=H-m;const q=o.qr?Math.min(unten-y,Math.round(W*0.3)):0;
    const zh=Math.max(Math.round(ref*0.072),Math.min(Math.round(ref*0.095),Math.floor((unten-y)/4)));const max=Math.min(f.werte,Math.floor((unten-y)/zh));
    const liste=zeilen(s,o,max);werte(ctx,liste,m,unten-liste.length*zh,innen-q-Math.round(ref*0.03),zh,ref);if(q)qr(ctx,o.qr,W-m-q,unten-q,q);return liste.weg}
  /* Compact (40×25): Logo, QR links, rechts Balken, Farbe (zweizeilig), Hex – ohne Werte */
  function kompakt(ctx,s,o,img,W,H,hex){const ref=H;const m=Math.round(ref*0.06);let y=m;const innen=W-2*m;
    const kh=Math.round(ref*0.2);kopf(ctx,s,img,m,y,innen,kh);y+=kh+Math.round(ref*0.05);const unten=H-m;
    const q=o.qr?unten-y:0;const qg=q?qr(ctx,o.qr,m,y,q,"oben-links")||0:0;const x=m+(qg?qg+Math.round(ref*0.06):0);const b=W-m-x;
    const bh=Math.round(ref*0.17);balken(ctx,s,x,y,b,bh);let yy=y+bh;let g=Math.round(ref*0.13);let z=umbrechen(ctx,farbeVon(s),b,g,500,hex?2:3);
    while(g>Math.round(ref*0.09)&&z[z.length-1].endsWith("…")){g--;z=umbrechen(ctx,farbeVon(s),b,g,500,hex?2:3)}z.forEach(t=>{yy+=Math.round(g*1.12);text(ctx,t,x,yy,g,500)});
    if(hex){yy+=Math.round(g*1.15);text(ctx,hex,x,Math.min(yy,unten-2),Math.round(g*0.9),500)}
    return slotsVon(o).map(k=>(FELDER.find(x=>x[0]===k)||[k,k])[1])}
  /* Slim (40×12): QR links, Logo oben, Balken + Farbe */
  function slim(ctx,s,o,img,W,H,hex){const m=Math.round(H*0.08);const q=o.qr?H-2*m:0;const qg=q?qr(ctx,o.qr,m,m,q,"oben-links"):0;
    const x=m+(qg?qg+m:0);const b=W-m-x;const kh=Math.round(H*0.3);kopf(ctx,s,img,x,m,b,kh);
    const bh=Math.round(H*0.28);const y=m+kh+Math.round(H*0.04);ctx.font=`700 ${Math.round(bh*0.6)}px ${SCHRIFT}`;const hexB=hex?ctx.measureText(hex).width+m:0;
    balken(ctx,s,x,y,b-hexB,bh);if(hex)text(ctx,hex,W-m,y+Math.round(bh*0.75),Math.round(bh*0.6),700,"#000","right");
    const fp=passend(ctx,farbeVon(s),b,Math.round(H*0.24),700,9);text(ctx,fp.text,x,H-m,fp.g,700);
    return slotsVon(o).map(k=>(FELDER.find(x=>x[0]===k)||[k,k])[1])}
  /* Für Slicer: kleine Kopfzeile, Werte groß im 2×2-Raster (Name klein darüber), QR rechts */
  function slicer(ctx,s,o,img,W,H,hex){const ref=H;const m=Math.round(ref*0.055);const innen=W-2*m;let y=m;
    const kh=Math.round(ref*0.13);kopf(ctx,s,img,m,y,Math.round(innen*0.6),kh);y+=kh+Math.round(ref*0.02);
    const bh=Math.round(ref*0.14);ctx.font=`700 ${Math.round(bh*0.55)}px ${SCHRIFT}`;const hexB=hex?ctx.measureText(hex).width+Math.round(ref*0.03):0;
    balken(ctx,s,m,y,innen-hexB,bh);if(hex)text(ctx,hex,W-m,y+Math.round(bh*0.72),Math.round(bh*0.55),700,"#000","right");y+=bh+Math.round(ref*0.03);
    const fp=passend(ctx,farbeVon(s),innen,Math.round(ref*0.09),700,9);y+=fp.g;text(ctx,fp.text,m,y,fp.g,700);y+=Math.round(ref*0.03);
    const unten=H-m;const q=o.qr?Math.min(unten-y,Math.round(W*0.27)):0;const breite=innen-(q?q+Math.round(ref*0.03):0);
    const liste=zeilen(s,o,4);const sp=Math.floor(breite/2);const zh=Math.floor((unten-y)/2);
    liste.forEach((z,i)=>werteGestapelt(ctx,[z],m+(i%2)*sp,y+Math.floor(i/2)*zh,sp-Math.round(ref*0.03),zh));
    if(q)qr(ctx,o.qr,W-m-q,unten-q,q);return liste.weg}
  /* Vertikal (30×40): Werte über dem QR – nie daneben, damit nichts abgeschnitten wird */
  function hoch(ctx,s,o,img,W,H,hex,f,fett=false,max=f.werte){const ref=W;const m=Math.round(ref*0.055);const innen=W-2*m;let y=m;
    const kh=Math.round(ref*(fett?0.2:0.17));kopf(ctx,s,img,m,y,innen,kh);y+=kh+Math.round(ref*0.03);
    const bh=Math.round(ref*(fett?0.22:0.19));balken(ctx,s,m,y,innen,bh);y+=bh+Math.round(ref*0.03);
    // QR-Code rechts neben Farbe und Hex-Wert, darunter die Werte als ruhige Tabelle in voller Breite:
    // Name links, Wert in einer Spalte, alle Zeilen gleich groß.
    const gap=Math.round(ref*0.04);const unten=H-m;const q=o.qr?Math.round(W*(fett?0.34:0.32)):0;const yQ=y;
    const fb=innen-(q?q+gap:0);const fg0=Math.round(ref*(fett?0.13:0.11));const ein=passend(ctx,farbeVon(s),fb,fg0,700,Math.round(fg0*0.7/K));
    const z=ein.text.endsWith("…")?umbrechen(ctx,farbeVon(s),fb,fg0,700,2):[ein.text];const fg=z.length>1?fg0:ein.g;
    // Farbe und Hex-Wert als Block senkrecht mittig neben dem QR-Code – kurze Namen rutschen nicht an den Balken,
    // lange (zweizeilige) füllen die Höhe des QR-Codes
    const blockH=Math.round(fg*0.74)+(z.length-1)*Math.round(fg*1.02)+(hex?Math.round(ref*0.095):0);
    y+=Math.max(0,Math.round((q-blockH)/2))-Math.round(fg*0.12);
    z.forEach((t,i)=>{y+=Math.round(fg*(i?1.02:0.86));text(ctx,t,m,y,fg,700)});
    if(hex){const hp=passend(ctx,hex,fb,Math.round(ref*0.085),700,8);y+=Math.round(ref*0.095);text(ctx,hp.text,m,y,hp.g,700)}
    if(q)qr(ctx,o.qr,W-m-q,yQ,q,"oben-rechts");
    y=Math.max(y+Math.round(ref*0.04),yQ+q+gap);
    const platz=unten-y;const alle=zeilen(s,o,max);
    const zh=Math.max(Math.round(ref*0.085),Math.min(Math.round(ref*(fett?0.15:0.125)),Math.floor(platz/Math.max(1,alle.length))));
    const liste=zeilen(s,o,Math.min(max,Math.floor(platz/zh)));
    if(liste.length){ctx.fillStyle="#000";ctx.fillRect(m,y-Math.round(gap*0.6),innen,Math.max(1,Math.round(K)));
      // Name klein und leicht, Wert groß und fett – alle Werte gleich groß, damit die Spalte ruhig wirkt
      const gn=Math.round(zh*0.56),gw0=Math.round(zh*0.8);ctx.font=`500 ${gn}px ${SCHRIFT}`;
      const sp=Math.max(0,...liste.filter(([nm])=>nm).map(([nm])=>ctx.measureText(nm).width))+Math.round(ref*0.04);
      const gw=Math.min(gw0,...liste.map(([nm,w])=>passend(ctx,w,innen-(nm?sp:0),gw0,800,8).g));
      liste.forEach(([nm,w],i)=>{const zy=y+(i+1)*zh-Math.round(zh*0.28);if(nm)text(ctx,nm,m,zy,gn,500);
        const p=passend(ctx,w,innen-(nm?sp:0),gw,800,8);text(ctx,p.text,m+(nm?sp:0),zy,p.g,800)})}
    return liste.weg}
  function hochfett(ctx,s,o,img,W,H,hex,f){return hoch(ctx,s,o,img,W,H,hex,f,true,2)}
  function hochkompakt(ctx,s,o,img,W,H,hex,f){return hoch(ctx,s,o,img,W,H,hex,f,false,2)}

  function schwarzWeiss(ctx,W,H){try{const d=ctx.getImageData(0,0,W,H);const p=d.data;
    for(let i=0;i<p.length;i+=4){const a=p[i+3]/255;const l=(0.299*p[i]+0.587*p[i+1]+0.114*p[i+2])*a+255*(1-a);const v=l<140?0:255;p[i]=p[i+1]=p[i+2]=v;p[i+3]=255}
    ctx.putImageData(d,0,0)}catch(e){}}
  function aufrecht(canvas){if(canvas.dataset.drehen!=="1")return canvas;const g=document.createElement("canvas");g.width=canvas.height;g.height=canvas.width;const c=g.getContext("2d");
    c.fillStyle="#fff";c.fillRect(0,0,g.width,g.height);c.translate(g.width,0);c.rotate(Math.PI/2);c.drawImage(canvas,0,0);return g}
  function raster(canvas){const dpi=Number(canvas.dataset.dpi)||203;canvas=aufrecht(canvas);const W=canvas.width,H=canvas.height;const bpr=Math.ceil(W/8);const p=canvas.getContext("2d").getImageData(0,0,W,H).data;const out=new Uint8Array(bpr*H);
    for(let y=0;y<H;y++)for(let x=0;x<W;x++){if(p[(y*W+x)*4]<128)out[y*bpr+(x>>3)]|=0x80>>(x&7)}
    let bin="";for(let i=0;i<out.length;i+=0x8000)bin+=String.fromCharCode.apply(null,out.subarray(i,i+0x8000));return {breite:bpr*8,hoehe:H,daten:btoa(bin),dpi}}
  function png(canvas,faktor=4){const g=document.createElement("canvas");g.width=canvas.width*faktor;g.height=canvas.height*faktor;const c=g.getContext("2d");c.imageSmoothingEnabled=false;c.drawImage(canvas,0,0,g.width,g.height);
    return new Promise(ok=>g.toBlob(b=>ok(b),"image/png"))}
  /* Beispiel für den Testdruck */
  const TEST={hersteller:"FilamentLager",material:"PLA",besonderheiten:["Test"],farbe:"Testetikett",hex:"#4CAF50",fcode:"",duese_min:190,duese_max:230,bett_min:55,bett_max:65,
    flow:0.98,td:-1,mvs:21,trocknen_c:-1,trocknen_h:-1,durchmesser:1.75,gewicht:1000,leer_g:250,ort:"Regal 1",getrocknet:new Date().toISOString().slice(0,10)};
  return {FORMATE,KOPF_MM,formatVon,hatWert,FELDER,STANDARD_FELDER,STANDARD_SLOTS,trocknen,datum,zeichnen,raster,png,TEST}
})();

