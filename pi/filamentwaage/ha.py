"""Home Assistant über die REST-API (nur Standardbibliothek)."""
from __future__ import annotations

import json
import ssl
import urllib.error
import urllib.request

from . import modell


class HaFehler(Exception):
    pass


class HaClient:
    def __init__(self, url, token, todo, zertifikat_pruefen=True, timeout=10):
        self.url = url.rstrip("/")
        self.token = token
        self.todo = todo
        self.timeout = timeout
        self.ssl = None
        if self.url.startswith("https://") and not zertifikat_pruefen:
            self.ssl = ssl.create_default_context()
            self.ssl.check_hostname = False
            self.ssl.verify_mode = ssl.CERT_NONE

    def _post(self, pfad, daten):
        req = urllib.request.Request(
            self.url + pfad,
            data=json.dumps(daten).encode(),
            headers={"Authorization": f"Bearer {self.token}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout, context=self.ssl) as r:
                return json.loads(r.read().decode() or "null")
        except urllib.error.HTTPError as e:
            if e.code == 401:
                raise HaFehler("Token abgelehnt (401)") from e
            raise HaFehler(f"Home Assistant meldet {e.code}") from e
        except (urllib.error.URLError, OSError) as e:
            raise HaFehler(f"Home Assistant nicht erreichbar ({getattr(e, 'reason', e)})") from e

    def lager_laden(self):
        antwort = self._post("/api/services/todo/get_items?return_response", {"entity_id": self.todo})
        try:
            return modell.lager_aus_antwort(antwort, self.todo)
        except (ValueError, AttributeError) as e:
            raise HaFehler(f"Antwort nicht lesbar – gibt es {self.todo}?") from e

    def beschreibung_setzen(self, item_uid, beschreibung):
        self._post("/api/services/todo/update_item",
                   {"entity_id": self.todo, "item": item_uid, "description": beschreibung})

    def vorlage(self, template):
        """Rendert ein Template in Home Assistant; die Vorlagen hier geben JSON aus."""
        return self._post("/api/template", {"template": template})

    def verbrauch(self):
        """Einträge der Verbrauchsliste von „Ernsthaft gelagert“ (todo.filament_verbrauch)."""
        liste = self.todo + "_verbrauch"
        return liste, self._post("/api/services/todo/get_items?return_response", {"entity_id": liste})
