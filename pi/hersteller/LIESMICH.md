# Herstellerbilder für die Etiketten

Bilder in diesem Ordner druckt die Waage oben aufs Etikett, statt des Herstellernamens.

- Dateiname = Hersteller in Kleinbuchstaben, Leer- und Sonderzeichen als Bindestrich,
  Umlaute ausgeschrieben: `Bambu Lab` → `bambu-lab.png`, `eSUN` → `esun.png`, `Polymaker` → `polymaker.jpg`
- PNG, JPG oder WebP; am besten schwarz auf weiß oder transparent, querformatig, etwa 400 × 80 Punkte
- Das Etikett wird schwarz-weiß gedruckt: Farben werden hell oder dunkel – kräftige Kontraste wirken am besten

**Sensorplatine (ESP32), Logos von außen:** Die Waage kann Logos auch erst bei Bedarf laden – Standard ist der Ordner
`/config/www/hersteller/` in Home Assistant (erreichbar als `http://…:8123/local/hersteller/bambu-lab.png`).
Diesen Ordner einfach mit den Bildern von hier füllen (Samba-Freigabe oder File-Editor). Eine andere Adresse,
z. B. auf der NAS, lässt sich unter *Einstellungen → Etiketten → Bildquelle* eintragen.

**Sensorplatine (ESP32), mitgeliefert:** Dort ist nur Platz für eine Auswahl – welche Hersteller mitkommen, steht in
`auf-der-waage.txt` (ein Name pro Zeile, wie im Lager). Die Bilder kommen beim Build mit aufs Gerät – Bild hier ablegen, auf GitHub hochladen,
den neuen Webflasher aus dem Branch `webflasher-bin` nehmen und flashen. Pro Bild höchstens 150 KB, zusammen
am besten unter 500 KB (das Dateisystem der Platine hat 1,4 MB, die Oberfläche braucht davon 770 KB).

`install.sh` kopiert den Ordner nach `/opt/filamentwaage/hersteller`. Ein Bild, das du in der Waage unter
**Einstellungen → Etiketten** hochlädst, hat Vorrang vor dem Bild hier.

Liegen hier Logos anderer Firmen, das Repository bitte privat lassen.
