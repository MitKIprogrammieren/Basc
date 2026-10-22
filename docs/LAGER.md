# Was die Waage in deinem Filamentlager ändert

Die Waage arbeitet direkt mit der Todo-Liste von „Ernsthaft gelagert“ (`todo.filament`) in
Home Assistant. Jede Sorte ist ein Eintrag, die Daten stehen als JSON in der Beschreibung.

## Felder

Die Waage liest die Felder der Karte (`hersteller`, `material`, `farbe`, `hex`, `gewicht`, `spulen`,
`refills`, `reste`, Temperaturen, Codes …) und schreibt nur:

| Feld | Bedeutung |
|---|---|
| `spulen` | Anzahl Spulen (wird bei einer leeren Spule um 1 kleiner) |
| `refills` | wird nur gekürzt, wenn es mehr Refills als Spulen gäbe |
| `reste` | Restgewichte angebrochener Spulen in g |
| `nfc` *(neu)* | Liste der NFC-UIDs, die zu dieser Sorte gehören |
| `waage` *(neu)* | zuletzt gewogener Rest je Spule: `{"04C0FFEE112233": 562}` |
| `leer_g` *(optional, neu)* | Gewicht der leeren Spule für diese Sorte, sonst gilt `leerspule_g` aus der Konfiguration (250 g) |

Alle anderen Felder bleiben unverändert, auch unbekannte. Vor jeder Änderung lädt die Waage den
aktuellen Stand aus Home Assistant, damit sie keine Änderungen von Handy oder Karte überschreibt.

## Rest speichern

Gemessen wird **netto** = Gewicht auf dem Teller − Leerspule.

| Situation | Was passiert |
|---|---|
| netto ≥ Sollgewicht − 15 g | Spule ist voll: nichts ändern (war sie als angebrochen gemerkt, wird ihr Rest gestrichen) |
| netto ≤ 10 g | Spule ist leer: ihr Rest wird gestrichen, `spulen` − 1 |
| Spule mit bekanntem Tag | genau ihr Rest wird ersetzt |
| Spule ohne Tag | der kleinste Rest, der noch größer ist als die Messung, wird ersetzt |
| kein passender Rest, noch volle Spulen da | eine volle Spule wurde angebrochen: neuer Rest |
| `spulen` = 0, aber Spule liegt auf | Spule wird ergänzt |

## NFC-Tags

- Bambu-Lab-Spulen haben zwei verschlüsselte MIFARE-Tags. Die Waage liest nur die **UID**, das reicht
  zum Wiedererkennen. Beim ersten Auflegen fragt sie, zu welcher Sorte die Spule gehört.
- TigerTag- und andere NTAG-Tags werden genauso über die UID zugeordnet.
- Ein Tag gehört immer nur zu einer Sorte. Wird er einer anderen zugeordnet, entfernt die Waage
  ihn bei der alten Sorte.
- Jede Spule hat ihre eigene UID. Eine Bambu-Spule mit zwei Tags muss nur mit einer Seite zugeordnet
  werden, wenn sie immer gleich herum aufgelegt wird – sonst beide Seiten einmal zuordnen.

## Etiketten

Fürs Etikett liest die Waage zusätzlich – wenn vorhanden – `bett_min`, `bett_max`, `flow` und `td`
(Transmission Distance). Fehlen sie, bleiben die Zeilen einfach weg. Geschrieben wird dabei nichts.
