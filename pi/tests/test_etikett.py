"""Tests für den Etikettendruck (Phomemo-Befehle, Herstellerbilder).
Aufruf im Ordner pi/:  python3 -m unittest discover -s tests
"""
import base64
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from filamentwaage import etiketten, phomemo  # noqa: E402

PNG = b"\x89PNG\r\n\x1a\n" + b"\x00" * 20


class PhomemoTest(unittest.TestCase):
    def test_befehle(self):
        daten = bytes(range(48)) * 2  # 384 Punkte breit, 2 Zeilen
        b = phomemo.befehle(384, 2, daten, dichte=12)
        self.assertEqual(b[:4], bytes([0x1B, 0x4E, 0x0D, 5]))
        self.assertEqual(b[4:8], bytes([0x1B, 0x4E, 0x04, 12]))
        self.assertEqual(b[8:11], bytes([0x1F, 0x11, phomemo.PAPIER_LUECKE]))
        self.assertEqual(b[11:19], bytes([0x1D, 0x76, 0x30, 0x00, 48, 0, 2, 0]))
        self.assertEqual(b[19:19 + len(daten)], daten)
        self.assertTrue(b.endswith(bytes([0x1F, 0xF0, 0x05, 0x00, 0x1F, 0xF0, 0x03, 0x00])))

    def test_befehle_falsche_groesse(self):
        with self.assertRaises(phomemo.DruckFehler):
            phomemo.befehle(384, 2, b"\x00" * 10)

    def test_kopfbreite_beschneiden(self):
        zeile = bytes([0xAA]) + bytes(range(48)) + bytes([0xBB])   # 400 Punkte
        b, d = phomemo.auf_kopfbreite(400, 1, zeile, 384)
        self.assertEqual(b, 384)
        self.assertEqual(d, bytes(range(48)))

    def test_kopfbreite_auffuellen(self):
        b, d = phomemo.auf_kopfbreite(240, 1, b"\xff" * 30, 384)
        self.assertEqual(b, 384)
        self.assertEqual(d, bytes(9) + b"\xff" * 30 + bytes(9))

    def test_name_erkennen(self):
        for n in ("M110", "M20Pro", "Q30S", "Phomemo-M220"):
            self.assertTrue(phomemo.wahrscheinlich_phomemo(n), n)
        for n in ("iPhone", "Mi Band", "JBL Flip"):
            self.assertFalse(phomemo.wahrscheinlich_phomemo(n), n)

    def test_datei(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "x.bin")
            phomemo.senden(b"abc", "datei", datei=p)
            with open(p, "rb") as f:
                self.assertEqual(f.read(), b"abc")


class HerstellerbildTest(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.repo = os.path.join(self.t.name, "repo")
        os.makedirs(self.repo)
        self.d = etiketten.EtikettDienst({"ordner": self.t.name, "bilder_repo": self.repo}, lambda *a, **k: None)

    def tearDown(self):
        self.t.cleanup()

    def test_kurz(self):
        self.assertEqual(etiketten.kurz("Bambu Lab"), "bambu-lab")
        self.assertEqual(etiketten.kurz("eSUN"), "esun")
        self.assertEqual(etiketten.kurz("Grüne Rolle & Co."), "gruene-rolle-co")

    def test_speichern_und_vorrang(self):
        with open(os.path.join(self.repo, "polymaker.png"), "wb") as f:
            f.write(PNG)
        self.assertTrue(self.d.bild_pfad("Polymaker").startswith(self.repo))
        self.assertIsNone(self.d.bild_pfad("Polymaker", nur_eigen=True))
        self.d.bild_speichern("Polymaker", b"\xff\xd8\xff" + b"\x00" * 10)
        self.assertTrue(self.d.bild_pfad("Polymaker").endswith("hersteller/polymaker.jpg"))
        self.d.bild_loeschen("Polymaker")
        self.assertTrue(self.d.bild_pfad("Polymaker").startswith(self.repo))

    def test_nur_bilder(self):
        with self.assertRaises(ValueError):
            self.d.bild_speichern("X", b"<svg></svg>")

    def test_einstellungen(self):
        self.d.setzen({"format": "vertikal", "dichte": 99, "adresse": "AA:BB", "name": "M220"})
        w = self.d.einstellungen([])
        self.assertEqual((w["format"], w["dichte"], w["adresse"]), ("vertikal", 15, "AA:BB"))
        neu = etiketten.EtikettDienst({"ordner": self.t.name}, lambda *a, **k: None)
        self.assertEqual(neu.werte["format"], "vertikal")

    def test_auftrag_pruefen(self):
        self.d.drucken({"breite": 16, "hoehe": 2, "daten": base64.b64encode(b"\x00" * 4).decode()})
        with self.assertRaises(ValueError):
            self.d.drucken({"breite": 16, "hoehe": 2, "daten": base64.b64encode(b"\x00").decode()})


if __name__ == "__main__":
    unittest.main()
