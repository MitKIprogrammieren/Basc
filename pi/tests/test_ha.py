"""HA-Client gegen einen nachgebauten Home-Assistant-Server testen."""
import json
import os
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from filamentwaage import modell  # noqa: E402
from filamentwaage.ha import HaClient, HaFehler  # noqa: E402

TESTDATEN = os.path.join(os.path.dirname(__file__), "..", "..", "testdaten", "todo_filament.json")
TOKEN = "geheim"


class FakeHa(BaseHTTPRequestHandler):
    items = None

    def log_message(self, *a):
        pass

    def do_POST(self):
        if self.headers.get("Authorization") != f"Bearer {TOKEN}":
            self.send_response(401)
            self.end_headers()
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/api/services/todo/get_items?return_response":
            antwort = {"changed_states": [], "service_response": {body["entity_id"]: {"items": FakeHa.items}}}
        elif self.path == "/api/services/todo/update_item":
            for it in FakeHa.items:
                if it["uid"] == body["item"]:
                    it["description"] = body["description"]
            antwort = []
        else:
            self.send_response(404)
            self.end_headers()
            return
        daten = json.dumps(antwort).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(daten)))
        self.end_headers()
        self.wfile.write(daten)


class HaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(TESTDATEN, encoding="utf-8") as f:
            FakeHa.items = json.load(f)["service_response"]["todo.filament"]["items"]
        cls.srv = HTTPServer(("127.0.0.1", 0), FakeHa)
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.srv.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()

    def test_laden_und_schreiben(self):
        ha = HaClient(self.url, TOKEN, "todo.filament")
        sorten = ha.lager_laden()
        self.assertEqual(len(sorten), 25)
        bg = modell.finde_uid(sorten, "uid-001")
        a = modell.rest_speichern(bg.beschreibung, 333, "04ZZ")
        ha.beschreibung_setzen(bg.uid, a.beschreibung)
        neu = modell.finde_uid(ha.lager_laden(), "uid-001")
        self.assertEqual(neu.reste, [333])
        self.assertEqual(json.loads(neu.beschreibung)["waage"], {"04ZZ": 333})

    def test_falsches_token(self):
        with self.assertRaisesRegex(HaFehler, "401"):
            HaClient(self.url, "falsch", "todo.filament").lager_laden()

    def test_nicht_erreichbar(self):
        with self.assertRaisesRegex(HaFehler, "nicht erreichbar"):
            HaClient("http://127.0.0.1:1", TOKEN, "todo.filament", timeout=1).lager_laden()


if __name__ == "__main__":
    unittest.main()
