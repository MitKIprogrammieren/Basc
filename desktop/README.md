# FilamentLager Studio – Mac-App

Die Schreibtisch-App zur FilamentLager-Waage – aufgebaut wie Tiger Studio Manager, aber für dein
Lager **„Ernsthaft gelagert“** in Home Assistant statt der TigerTag-Cloud.

| Bereich | Was er kann |
|---|---|
| **Wiegen** | Live-Gewicht der Waage, Spule am NFC-Tag erkennen, Rest mit einem Klick ins Lager schreiben, neue Tags zuordnen (mit Vorschlag aus dem AMS) |
| **Lager** | alle Sorten aus `todo.filament`: Suche, Filter, Sortierung; Spulen ±, Reste streichen, Leerspulengewicht, NFC-Tags verwalten |
| **Drucker** | alle Bambu-Drucker aus Home Assistant mit Status, Fortschritt, Temperaturen und jedem AMS-Slot; Sorte aus dem Lager in einen Slot eintragen (`bambu_lab.set_filament`), Tag neu lesen |
| **Regale** | eigene Regale mit Ebenen und Plätzen, Spulen per Ziehen einsortieren, „wo liegt …?“ |
| **Etiketten** | Spulen-Etiketten mit QR-Code; Druck auf dem Phomemo M20Pro per Bluetooth, über den Mac-Druckdialog oder als PNG |

## Installieren

1. Unter **Actions → FilamentLager Studio (Mac)** den neuesten Lauf öffnen und `FilamentLager-Studio-mac` laden
   (oder unter *Releases*, sobald es einen Tag `studio-v…` gibt).
2. Die `.dmg` für deinen Mac öffnen (`arm64` = Apple Silicon M1/M2/M3/M4, `x64` = Intel) und die App in *Programme* ziehen.
3. Erster Start: **Rechtsklick auf die App → Öffnen → Öffnen** (die App hat kein Apple-Entwicklerzertifikat).
   Sagt macOS „beschädigt“, einmal im Terminal: `xattr -cr "/Applications/FilamentLager Studio.app"`

## Einrichten

**Home Assistant:** Einstellungen → Adresse (z. B. `http://homeassistant.local:8123` oder deine Nabu-Casa-Adresse) und
ein langlebiges Zugriffstoken (HA → Profil → Sicherheit). Das Token liegt verschlüsselt im Schlüsselbund.
Ohne Token läuft die App im Demo-Modus mit Beispieldaten.

**Waage** – zwei Wege:

- **Über Pad 7 / Pi** (empfohlen): Die App liest das Gewicht von der Waagen-Oberfläche, das Pad 7 bleibt nutzbar.
  Auf dem Pad 7 in `/etc/filamentwaage/config.toml`:
  ```toml
  [server]
  adresse = "0.0.0.0"
  ```
  dann `sudo systemctl restart filamentwaage`. In der App: Adresse des Pads, Port 8080.
- **Sensorplatine direkt** (Port 4711): ohne Pad 7. Die Platine bedient nur eine Gegenstelle – ein Pad 7 wird dabei getrennt.

**Drucker:** kommen automatisch aus der Integration *Bambu Lab* (ha-bambulab). Namen und Ausblenden über ⋯ an jeder Karte.

**Etiketten (Phomemo M20Pro):** Bluetooth am Mac an, Drucker einschalten, *Auf Phomemo drucken* – beim ersten Mal den
Drucker in der Liste wählen. Der Bluetooth-Druck nutzt das Protokoll der Phomemo-M-Reihe und ist **mit dem M20Pro noch
nicht getestet**. Bleibt das Etikett leer oder versetzt: Einstellungen → Druckbreite (576/600) und Dichte anpassen.
Immer möglich: *Als PNG sichern* und mit der Phomemo-App drucken.

## Was die App im Lager ändert

Genau dieselben Felder wie die Waage – `spulen`, `refills` (nur gekürzt), `reste`, `nfc`, `waage`, `leer_g` – nach denselben
Regeln (siehe [../docs/LAGER.md](../docs/LAGER.md)). Vor jeder Änderung lädt die App den aktuellen Stand aus Home Assistant.
Das Datenmodell `services/modell.js` ist die dritte Fassung neben `pi/filamentwaage/modell.py` und
`firmware/display-5inch/src/model.cpp`; ein Vergleichstest mit 4000 Zufallsfällen ergab keine Abweichung zur Python-Fassung.

Regale liegen nur auf dem Mac (`~/Library/Application Support/FilamentLager Studio/regale.json`) und lassen sich als Datei sichern.

## Entwickeln

```bash
cd desktop
npm install
npm start          # App starten
npm run demo       # mit Beispieldaten
npm test           # Datenmodell, Drucker-Erkennung, HA-Anbindung (gegen nachgebautes HA), Waage
npm run vorschau   # Oberfläche im Browser ohne Electron: http://127.0.0.1:5173
npm run build:mac  # .dmg bauen
```

| Datei | Inhalt |
|---|---|
| `main.js` | Hauptprozess: Einstellungen, Lager, Drucker, Waage, Etikettendruck |
| `services/modell.js` | Regeln für todo.filament (wie modell.py) |
| `services/ha.js` | Home Assistant über WebSocket |
| `services/drucker.js` | Bambu-Drucker und AMS aus der Entity-Registry (sprachunabhängig über unique_id) |
| `services/waage.js` | Waage über Pad 7/Pi (Ereignisstrom) oder direkt (TCP 4711) |
| `renderer/` | Oberfläche (HTML/CSS/JS ohne Framework, Schrift Montserrat) |

Angelehnt an [Tiger Studio Manager](https://github.com/TigerTag-Project/TigerTag-Studio-Manager) (MIT) – eigene, schlanke
App ohne Code daraus und ohne Verbindung zum TigerTag-Projekt; „TigerTag“ ist eine Marke des TigerTag-Projekts.
