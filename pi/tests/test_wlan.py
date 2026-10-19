"""SensorWlan gegen eine nachgebaute Sensorplatine (TCP, Protokoll aus docs/PROTOKOLL.md)."""
import json
import os
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from filamentwaage.sensoren import SensorWlan  # noqa: E402


class FakeWaage:
    """Antwortet wie die Firmware: nur auf Anfragen, eine JSON-Zeile pro Nachricht."""

    def __init__(self):
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(1)
        self.port = self.srv.getsockname()[1]
        self.gramm, self.tag_r = 812.4, "04C0FFEE112233"
        self.befehle = []
        self.verbindungen = 0
        self.stumm = False
        threading.Thread(target=self._lauf, daemon=True).start()

    def _lauf(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            self.verbindungen += 1
            c.sendall(b'{"t":"hallo","fw":"1.1.0","nfc_l":true,"nfc_r":true,"cal":1}\n')
            puf = b""
            with c:
                while True:
                    try:
                        d = c.recv(256)
                    except OSError:
                        break
                    if not d:
                        break
                    puf += d
                    while b"\n" in puf:
                        z, puf = puf.split(b"\n", 1)
                        b = json.loads(z)
                        self.befehle.append(b["c"])
                        if self.stumm:
                            continue
                        if b["c"] == "p":
                            antw = {"t": "s", "g": self.gramm, "st": 1, "cal": 1, "L": "", "R": self.tag_r}
                        elif b["c"] == "tara":
                            antw = {"t": "ok", "c": "tara"}
                        else:
                            antw = {"t": "err", "c": b["c"], "msg": "kein Gewicht erkannt"}
                        c.sendall((json.dumps(antw) + "\n").encode())

    def schliessen(self):
        self.srv.close()


def warten(bed, s=3.0):
    ende = time.monotonic() + s
    while time.monotonic() < ende:
        if bed():
            return True
        time.sleep(0.02)
    return False


class WlanTest(unittest.TestCase):
    def setUp(self):
        self.waage = FakeWaage()
        self.sensor = SensorWlan("127.0.0.1", self.waage.port)
        self.meldungen = []
        self.sensor.rueckmeldung = lambda ok, t: self.meldungen.append((ok, t))
        self.sensor.start()

    def tearDown(self):
        self.waage.schliessen()

    def test_messwert(self):
        self.assertTrue(warten(lambda: self.sensor.wert().verbunden))
        w = self.sensor.wert()
        self.assertAlmostEqual(w.gramm, 812.4)
        self.assertTrue(w.stabil and w.kalibriert)
        self.assertEqual(w.tag_r, "04C0FFEE112233")
        self.waage.gramm = 655.0
        self.assertTrue(warten(lambda: self.sensor.wert().gramm == 655.0))

    def test_tara_und_fehler(self):
        self.assertTrue(warten(lambda: self.sensor.wert().verbunden))
        self.sensor.tara()
        self.sensor.kalibrieren(1000)
        self.assertTrue(warten(lambda: len(self.meldungen) == 2))
        self.assertEqual(self.meldungen[0], (True, "Tara gesetzt"))
        self.assertFalse(self.meldungen[1][0])

    def test_stumme_waage_wird_getrennt(self):
        self.assertTrue(warten(lambda: self.sensor.wert().verbunden))
        self.waage.stumm = True
        self.assertTrue(warten(lambda: not self.sensor.wert().verbunden, 3))
        # nach 3 s ohne Antwort trennt der Sensor und verbindet nach 2 s neu
        self.assertTrue(warten(lambda: self.waage.verbindungen >= 2, 8))
        self.waage.stumm = False
        self.assertTrue(warten(lambda: self.sensor.wert().verbunden, 3))


if __name__ == "__main__":
    unittest.main()
