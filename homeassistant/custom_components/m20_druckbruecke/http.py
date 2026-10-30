"""HTTP-Schnittstelle wie die Pi-Druckbrücke – nur aus dem Heimnetz erreichbar.

    GET  /api/m20_druckbruecke/drucker     alle eingerichteten Drucker [{id, name, modell, erreichbar, kopf_mm, standard}]
    POST /api/m20_druckbruecke/drucken     {"breite", "hoehe", "daten" (Base64), "kopien", "dichte", "titel", "drucker"}
    POST /api/m20_druckbruecke/test        ?drucker=m110
    GET  /api/m20_druckbruecke/status      ?drucker=m110
    POST /api/m20_druckbruecke/freigeben   (ohne Wirkung – es gibt keine Dauerverbindung mehr)
    POST /api/m20_druckbruecke/verbinden   (fragt die Druckerinfos neu ab)

„drucker“ ist die Modellkennung (m20pro, m110 …) oder die Bluetooth-Adresse. Fehlt sie, druckt der
M20 Pro – so drucken ältere Waagen-Versionen weiter wie bisher.
"""
from __future__ import annotations

import base64
import ipaddress
import json

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.helpers.network import is_cloud_connection

from .const import API_PFAD, DOMAIN, STANDARD_MODELL, TEST
from .escpos import escpos_auftrag, escpos_test
from .tspl import tspl_auftrag

KOPF = {"Access-Control-Allow-Origin": "*"}


def _antwort(daten, code: int = 200) -> web.Response:
    return web.Response(text=json.dumps(daten, ensure_ascii=False), status=code,
                        content_type="application/json", headers=KOPF)


class DruckView(HomeAssistantView):
    """Ohne Anmeldung – die Waage kennt keinen HA-Token. Dafür nur Anfragen aus dem lokalen Netz."""

    url = API_PFAD + "/{aktion}"
    name = "api:m20_druckbruecke"
    requires_auth = False

    def __init__(self, hass: HomeAssistant, version: str) -> None:
        self.hass = hass
        self.version = version

    def _alle(self) -> list:
        return [d for d in self.hass.data.get(DOMAIN, {}).values() if hasattr(d, "modell")]

    def _standard(self):
        alle = self._alle()
        return next((d for d in alle if d.modell == STANDARD_MODELL), alle[0] if alle else None)

    def _drucker(self, wahl: str | None):
        """Drucker nach Modell oder Adresse; ohne Wahl der Standard (M20 Pro)."""
        wahl = (wahl or "").strip().lower().replace(" ", "").replace("-", "")
        if not wahl:
            return self._standard()
        for d in self._alle():
            if wahl in (d.modell, d.adresse.lower().replace(":", ""), d.name.lower().replace(" ", "")):
                return d
        for d in self._alle():
            if d.adresse.lower() == wahl:
                return d
        return None

    def _lokal(self, request: web.Request) -> bool:
        if is_cloud_connection(self.hass):
            return False
        try:
            ip = ipaddress.ip_address(request.remote or "")
        except ValueError:
            return False
        return ip.is_private or ip.is_loopback or ip.is_link_local

    def _liste(self) -> list:
        std = self._standard()
        return [{"id": d.modell, "name": d.name, "modell": d.modell, "adresse": d.adresse, "erreichbar": d.erreichbar,
                 "beschaeftigt": d.beschaeftigt, "kopf_mm": d.kopf_px // 8, "standard": d is std}
                for d in sorted(self._alle(), key=lambda d: (d is not std, d.name))]

    async def get(self, request: web.Request, aktion: str) -> web.Response:
        if not self._lokal(request):
            return _antwort({"ok": False, "text": "nur im Heimnetz"}, 403)
        if aktion == "drucker":
            return _antwort({"ok": True, "drucker": self._liste(), "version": self.version})
        drucker = self._drucker(request.query.get("drucker"))
        if drucker is None:
            return _antwort({"ok": False, "text": "Drucker nicht eingerichtet"}, 503)
        if aktion == "status":
            d = drucker.status(self.version)
            d["alle"] = self._liste()
            return _antwort(d)
        return _antwort({"ok": False, "text": "nicht gefunden"}, 404)

    async def post(self, request: web.Request, aktion: str) -> web.Response:
        if not self._lokal(request):
            return _antwort({"ok": False, "text": "nur im Heimnetz"}, 403)
        d = {}
        if aktion == "drucken":
            if (request.content_length or 0) > 2 * 1024 * 1024:
                return _antwort({"ok": False, "text": "zu groß"}, 413)
            try:
                d = json.loads(await request.read() or b"{}")
            except ValueError:
                return _antwort({"ok": False, "text": "Etikett ungültig"}, 400)
        drucker = self._drucker(d.get("drucker") or request.query.get("drucker"))
        if drucker is None:
            return _antwort({"ok": False, "text": "Diesen Drucker gibt es in Home Assistant nicht"}, 503)
        if aktion == "freigeben":
            return _antwort({"ok": True, "text": "Keine Dauerverbindung – der Drucker ist zwischen den Drucken frei"})
        if aktion == "verbinden":
            self.hass.async_create_task(drucker.async_infos())
            return _antwort({"ok": True, "text": "Frage den Drucker ab …"})
        if aktion == "test":
            auftrag = TEST if drucker.sprache == "tspl" else escpos_test(drucker.kopf_px)
            titel = "Testdruck"
        elif aktion == "drucken":
            try:
                breite, hoehe = int(d["breite"]), int(d["hoehe"])
                daten = base64.b64decode(d["daten"])
                if breite <= 0 or breite % 8 or hoehe <= 0 or len(daten) != breite // 8 * hoehe:
                    raise ValueError("Etikett unvollständig")
                if drucker.sprache == "tspl":
                    auftrag = await self.hass.async_add_executor_job(
                        tspl_auftrag, breite, hoehe, daten, d.get("dichte", 10), d.get("kopien", 1), drucker.dpi)
                else:
                    auftrag = await self.hass.async_add_executor_job(
                        escpos_auftrag, breite, hoehe, daten, d.get("dichte", 10), d.get("kopien", 1), drucker.kopf_px)
                titel = str(d.get("titel") or "Etikett")[:80]
            except (KeyError, ValueError, TypeError) as e:
                return _antwort({"ok": False, "text": "Etikett ungültig (%s)" % e}, 400)
        else:
            return _antwort({"ok": False, "text": "nicht gefunden"}, 404)
        if not drucker.auftrag_annehmen(auftrag, titel):
            return _antwort({"ok": False, "text": f"{drucker.name} ist noch beschäftigt"}, 503)
        return _antwort({"ok": True, "text": f"Druckauftrag an {drucker.name} angenommen", "drucker": drucker.modell}, 202)
