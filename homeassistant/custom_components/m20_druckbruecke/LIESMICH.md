# Druckbrücke in Home Assistant (Phomemo M20 Pro, M110 …)

Druckt die Etiketten der Waage und von SchichtLohn per Bluetooth LE über Home Assistant –
die Drucker hängen am ESPHome-Bluetooth-Proxy (AtomS3).

- Je Drucker ein Eintrag unter Einstellungen → Geräte & Dienste → „Druckbrücke Phomemo“.
  Neue Drucker meldet Home Assistant meist von selbst („Entdeckt“), sonst „Integration hinzufügen“.
- Der M20 Pro bleibt Standarddrucker; ohne Wahl druckt alles wie bisher dort.
- Schnittstelle (nur aus dem Heimnetz): `GET /api/m20_druckbruecke/drucker`,
  `POST /api/m20_druckbruecke/drucken` mit `"drucker": "m110"`.

Installieren: Ordner `m20_druckbruecke` nach `/config/custom_components/` kopieren und Home Assistant neu starten.
