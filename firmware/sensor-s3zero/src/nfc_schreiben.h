// NFC-Aufkleber über die Waage beschreiben – Aufträge kommen vom SchichtLohn-Server.
//
// Ablauf: In SchichtLohn tippt man bei einer Sorte „Über die Waage beschreiben“. Liegt danach ein NTAG-Aufkleber
// auf einem Leser, fragt die Waage alle 2 s beim Server nach (POST /api/nfc_auftrag/<waage> mit den UIDs).
// Der Server antwortet mit Seite, UID und Link; der NFC-Task schreibt den Link, die Waage meldet das Ergebnis
// (POST /api/nfc_auftrag/<waage>/ergebnis). Ohne SchichtLohn-Server (404) fragt die Waage nur alle 5 min.
#pragma once

#include <Arduino.h>

class HaClient;

namespace nfc_schreiben {
// NFC-Task: aktueller Tag auf Seite 0 (links) / 1 (rechts); uid leer = nichts da
void tag_stand(int seite, const String& uid, bool ntag);
// NFC-Task: liegt ein Auftrag für genau diesen Tag vor? Dann url füllen und true
bool auftrag_fuer(int seite, const String& uid, String& url);
// NFC-Task: Ergebnis nach dem Schreiben
void ergebnis(const String& uid, bool ok, const String& fehler);
// Netz-Task (oft aufrufen): fragt nach Aufträgen, solange ein NTAG aufliegt, und meldet Ergebnisse
void abfragen(HaClient& ha, const String& waage);
// true, wenn gerade ein NTAG aufliegt oder ein Ergebnis zu melden ist (sonst muss abfragen() nicht laufen)
bool etwas_zu_tun();
}  // namespace nfc_schreiben
