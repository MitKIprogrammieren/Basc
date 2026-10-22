// Webseite der Sensorplatine: http://filamentwaage.local bzw. die IP der Waage.
// Liegt komplett im Flash, braucht kein Internet. Fragt /api/status etwa 4× pro Sekunde ab.
#pragma once

static const char WEB_SEITE[] = R"rawliteral(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FilamentLager-Waage</title>
<style>
:root{--bg:#0d0f12;--panel:#15181c;--linie:#23272d;--rand:#3a4048;--rand2:#2a2f36;--text:#f1f3f5;--sub:#a3aab3;
--gruen:#4caf50;--gruen-t:#9fe3a2;--chip-g:#1e3024;--orange-t:#ffb74d;--rot:#ef5350}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Montserrat,system-ui,-apple-system,"Segoe UI",sans-serif}
.kopf{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:12px 18px;border-bottom:1px solid var(--linie);flex-wrap:wrap}
.links,.rechts{display:flex;align-items:center;gap:10px}.rechts{gap:14px;font-size:13px;color:var(--sub)}
.logo{width:20px;height:20px;border-radius:50%;border:2px solid var(--gruen);display:grid;place-items:center}
.logo span{width:6px;height:6px;border-radius:50%;background:var(--gruen)}
.titel{font-weight:700;font-size:17px}.sub{font-size:13px;color:var(--sub)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--rand);margin-right:5px}
.dot.an{background:var(--gruen)}.dot.aus{background:var(--rot)}
main{max-width:900px;margin:0 auto;padding:16px;display:grid;grid-template-columns:minmax(0,1.4fr) minmax(0,1fr);gap:14px}
@media(max-width:640px){main{grid-template-columns:minmax(0,1fr)}}
.panel{background:var(--panel);border:2px solid var(--rand2);border-radius:18px;padding:16px 18px}
.panel.gruen{border-color:var(--gruen)}
.ueber{font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;color:var(--sub)}
.zahl{display:flex;align-items:baseline;gap:8px;margin-top:6px}
#g{font-size:68px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}.einheit{font-size:24px;color:var(--sub)}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px}
.chip{font-size:12px;padding:3px 9px;border-radius:7px;background:var(--linie);color:var(--sub)}
.chip.g{background:var(--chip-g);color:var(--gruen-t)}.chip.o{border:1px solid rgba(255,167,38,.6);background:transparent;color:var(--orange-t)}
.knoepfe{display:flex;gap:10px;margin-top:18px}
button{font-family:inherit;font-size:16px;font-weight:600;border-radius:14px;height:50px;cursor:pointer;flex:1}
button:disabled{opacity:.4;cursor:default}
.primaer{background:var(--gruen);color:var(--bg);border:0}.primaer:active{background:#3d8b40}
.rahmen{background:transparent;color:var(--text);border:2px solid var(--rand)}.rahmen:active{background:var(--linie)}
.spalte{display:flex;flex-direction:column;gap:12px}
.tag{font-size:15px;margin-top:6px;color:var(--sub);font-family:ui-monospace,Menlo,monospace;word-break:break-all}
.tag.da{color:var(--gruen-t)}
.info{font-size:13px;color:var(--sub);line-height:1.8}.ok{color:var(--gruen-t)}.fehler{color:var(--rot)}
.meldung{min-height:20px;margin-top:12px;font-size:14px;color:var(--sub)}
.fuss{grid-column:1/-1;display:flex;justify-content:flex-end;gap:10px;flex-wrap:wrap}
.fuss button{flex:0 0 auto;height:36px;font-size:13px;font-weight:500;padding:0 14px;border-radius:9px;border-width:1px}
.getrennt{opacity:.45}
</style></head><body>
<div class="kopf">
 <div class="links"><div class="logo"><span></span></div><span class="titel">FilamentLager-Waage</span><span class="sub">Sensorplatine</span></div>
 <div class="rechts"><span><span class="dot" id="wdot"></span><span id="wlan">–</span></span><span id="fw">FW –</span></div>
</div>
<main id="app">
 <div class="panel" id="wkarte">
  <div class="ueber">Gewicht auf dem Teller</div>
  <div class="zahl"><span id="g">–</span><span class="einheit">g</span></div>
  <div class="chips"><span class="chip" id="cst">–</span><span class="chip" id="ccal">–</span></div>
  <div class="knoepfe">
   <button class="primaer" id="btara">Tara</button>
   <button class="rahmen" id="bkal">Kalibrieren…</button>
  </div>
  <div class="meldung" id="meldung"></div>
 </div>
 <div class="spalte">
  <div class="panel" id="pl"><div class="ueber">NFC links</div><div class="tag" id="tl">–</div></div>
  <div class="panel" id="pr"><div class="ueber">NFC rechts</div><div class="tag" id="tr">–</div></div>
  <div class="panel info" id="info">–</div>
 </div>
 <div class="fuss">
  <button class="rahmen" id="bwlan">WLAN neu einrichten</button>
  <button class="rahmen" id="bneu">Neu starten</button>
 </div>
</main>
<script>
const $=id=>document.getElementById(id);
let beschaeftigt=false,fehlerZaehler=0;
function zeigeTag(feld,karte,uid){
 $(feld).textContent=uid||'kein Tag';$(feld).classList.toggle('da',!!uid);$(karte).classList.toggle('gruen',!!uid);
}
function chip(el,an,textAn,textAus,warn){el.textContent=an?textAn:textAus;el.className='chip '+(an?'g':(warn?'o':''));}
function ja(x){return x?'<span class="ok">ok</span>':'<span class="fehler">fehlt</span>';}
async function status(){
 try{
  const r=await fetch('/api/status',{cache:'no-store',signal:AbortSignal.timeout(2000)});const s=await r.json();fehlerZaehler=0;
  $('app').classList.remove('getrennt');
  $('g').textContent=s.cal?s.g.toFixed(1).replace('.',','):'–';
  chip($('cst'),s.st,'ruhig','unruhig',false);chip($('ccal'),s.cal,'kalibriert','nicht kalibriert',true);
  $('wkarte').classList.toggle('gruen',!!s.st&&!!s.cal);
  zeigeTag('tl','pl',s.L);zeigeTag('tr','pr',s.R);
  $('fw').textContent='FW '+s.fw;$('wlan').textContent='WLAN '+s.rssi+' dBm';$('wdot').className='dot an';
  $('info').innerHTML='HX711 '+ja(s.hx711)+' · PN532 L '+ja(s.nfc_l)+' · R '+ja(s.nfc_r)+
   '<br>IP '+s.ip+' · Pad 7 '+(s.pad?'<span class="ok">verbunden</span>':'nicht verbunden');
  beschaeftigt=!!s.vorgang;$('btara').disabled=beschaeftigt;$('bkal').disabled=beschaeftigt;
  if(beschaeftigt)$('meldung').textContent=s.vorgang==='tara'?'Tara läuft – Teller nicht berühren…':'Kalibrieren läuft – nicht berühren…';
  else if(s.meldung)$('meldung').textContent=s.meldung;
 }catch(e){
  if(++fehlerZaehler>3){$('app').classList.add('getrennt');$('wdot').className='dot aus';$('wlan').textContent='keine Verbindung';}
 }
}
async function senden(pfad){
 try{const r=await fetch(pfad,{method:'POST'});const a=await r.json();if(a.msg)$('meldung').textContent=a.msg;return a;}
 catch(e){$('meldung').textContent='Waage nicht erreichbar.';return {};}
}
$('btara').onclick=()=>{$('meldung').textContent='Tara läuft…';senden('/api/tara');};
$('bkal').onclick=()=>{
 const w=prompt('Prüfgewicht auflegen und Gewicht in Gramm eingeben:','1000');if(w===null)return;
 const g=parseInt(w,10);if(!(g>=50)){$('meldung').textContent='Bitte mindestens 50 g eingeben.';return;}
 $('meldung').textContent='Kalibrieren läuft…';senden('/api/kal?g='+g);
};
$('bwlan').onclick=()=>{if(confirm('WLAN-Daten löschen? Danach öffnet die Waage wieder das WLAN „FilamentWaage-Setup“.'))senden('/api/wlan-reset');};
$('bneu').onclick=()=>{if(confirm('Waage neu starten?'))senden('/api/neustart');};
async function schleife(){await status();setTimeout(schleife,250);}
schleife();
</script></body></html>)rawliteral";
