"""Tests für das Datenmodell – gleiche Fälle wie test/host/test_model.cpp.
Aufruf im Ordner pi/:  python3 -m unittest discover -s tests
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from filamentwaage import modell as m  # noqa: E402

BLUE_GRAY = json.dumps({
    "v": 2, "hersteller": "Bambu Lab", "material": "PLA", "farbe": "Blue Gray", "hex": "#5b6579",
    "besonderheiten": ["Basic"], "gewicht": 1000, "spulen": 5, "refills": 0, "reste": [560], "codes": [],
    "codes_refill": [], "durchmesser": 1.75, "ausfuehrung": "spule", "duese_min": None, "duese_max": None,
    "bett_min": None, "bett_max": None, "trocknen_c": None, "trocknen_h": None, "fcode": "", "bambu_id": "GFA00"})
ASH_GRAY = json.dumps({
    "v": 2, "hersteller": "Bambu Lab", "material": "PLA", "farbe": "Ash Gray", "hex": "#9b9ea0",
    "besonderheiten": ["Matt"], "gewicht": 1000, "spulen": 4, "refills": 1, "reste": [],
    "codes": ["6975337030058"], "duese_min": 190, "duese_max": 230, "trocknen_c": 55, "trocknen_h": 8,
    "fcode": "11102", "bambu_id": "GFA01"})

TESTDATEN = os.path.join(os.path.dirname(__file__), "..", "..", "testdaten", "todo_filament.json")


class ModellTest(unittest.TestCase):
    def test_lager_laden(self):
        with open(TESTDATEN, encoding="utf-8") as f:
            sorten = m.lager_aus_antwort(json.load(f), "todo.filament")
        self.assertEqual(len(sorten), 25)  # Einstellungen ist keine Sorte
        bg = m.finde_nfc(sorten, "04C0FFEE112233")
        self.assertEqual(bg.kurzname, "PLA Blue Gray")
        self.assertEqual(bg.bestand_g, 4560)

    def test_rest_ohne_tag(self):
        a = m.rest_speichern(BLUE_GRAY, 540, "")
        d = json.loads(a.beschreibung)
        self.assertTrue(a.geaendert)
        self.assertEqual(d["reste"], [540])
        self.assertEqual(d["bambu_id"], "GFA00")
        self.assertIsNone(d["duese_min"])
        self.assertNotIn("waage", d)

    def test_rest_mit_tag(self):
        a = m.rest_speichern(BLUE_GRAY, 800, "04AA")
        d = json.loads(a.beschreibung)
        self.assertEqual(d["reste"], [560, 800])
        self.assertEqual(d["waage"], {"04AA": 800})
        a = m.rest_speichern(a.beschreibung, 640, "04AA")
        d = json.loads(a.beschreibung)
        self.assertEqual(d["reste"], [560, 640])
        a = m.rest_speichern(a.beschreibung, 3, "04AA")
        d = json.loads(a.beschreibung)
        self.assertEqual(d["spulen"], 4)
        self.assertEqual(d["reste"], [560])
        self.assertNotIn("waage", d)

    def test_voll_und_leer(self):
        self.assertFalse(m.rest_speichern(ASH_GRAY, 995, "").geaendert)
        d = json.loads(m.rest_speichern('{"gewicht":1000,"spulen":1,"refills":1,"reste":[]}', 0, "").beschreibung)
        self.assertEqual((d["spulen"], d["refills"]), (0, 0))
        d = json.loads(m.rest_speichern('{"gewicht":1000,"spulen":0,"reste":[]}', 400, "").beschreibung)
        self.assertEqual((d["spulen"], d["reste"]), (1, [400]))

    def test_tags(self):
        a = m.tag_zuordnen(ASH_GRAY, "04BB")
        self.assertTrue(a.geaendert)
        s = m.sorte_aus_eintrag("u", "x", a.beschreibung)
        self.assertEqual(s.nfc, ["04BB"])
        self.assertFalse(m.tag_zuordnen(a.beschreibung, "04BB").geaendert)
        b = m.tag_entfernen(a.beschreibung, "04BB")
        self.assertNotIn("nfc", json.loads(b.beschreibung))

    def test_spulen(self):
        self.assertEqual(json.loads(m.spulen_aendern(BLUE_GRAY, -1).beschreibung)["spulen"], 4)
        d = json.loads(m.spulen_aendern('{"spulen":1,"reste":[300]}', -1).beschreibung)
        self.assertEqual((d["spulen"], d["reste"]), (0, []))
        self.assertFalse(m.spulen_aendern('{"spulen":0}', -1).geaendert)


if __name__ == "__main__":
    unittest.main()
