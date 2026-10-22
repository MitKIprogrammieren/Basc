# Herstellerbilder für die Etiketten

Bilder in diesem Ordner druckt die Waage oben aufs Etikett, statt des Herstellernamens.

- Dateiname = Hersteller in Kleinbuchstaben, Leer- und Sonderzeichen als Bindestrich,
  Umlaute ausgeschrieben: `Bambu Lab` → `bambu-lab.png`, `eSUN` → `esun.png`, `Polymaker` → `polymaker.jpg`
- PNG, JPG oder WebP; am besten schwarz auf weiß oder transparent, querformatig, etwa 400 × 80 Punkte
- Das Etikett wird schwarz-weiß gedruckt: Farben werden hell oder dunkel – kräftige Kontraste wirken am besten

`install.sh` kopiert den Ordner nach `/opt/filamentwaage/hersteller`. Ein Bild, das du in der Waage unter
**Einstellungen → Etiketten** hochlädst, hat Vorrang vor dem Bild hier.

Liegen hier Logos anderer Firmen, das Repository bitte privat lassen.
