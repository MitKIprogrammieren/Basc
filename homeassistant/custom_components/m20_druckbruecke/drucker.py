"""Verbindung zu Phomemo-Etikettendruckern (M20 Pro, M110 …) über Bluetooth Low Energy (auch über ESPHome-Bluetooth-Proxys).

Pro Auftrag wird eine Verbindung aufgebaut und danach wieder getrennt – so belegt der Drucker
keinen Verbindungsplatz des Proxys dauerhaft.
"""
from __future__ import annotations

import asyncio
import logging
import re
import time
from collections.abc import Callable

from bleak_retry_connector import BleakClientWithServiceCache, establish_connection

from homeassistant.components import bluetooth
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.storage import Store

from .const import DOMAIN, INFO_FRAGEN, MODELLE, SIGNAL_UPDATE, STANDARD_MODELL, UUID_ANTWORT, UUID_SCHREIBEN

_LOGGER = logging.getLogger(__name__)
WARTESCHLANGE_MAX = 8


class M20Drucker:
    """Ein Phomemo-Etikettendrucker, angesprochen über HAs Bluetooth-Stack."""

    def __init__(self, hass: HomeAssistant, adresse: str, entry_id: str, modell: str = STANDARD_MODELL) -> None:
        self.hass = hass
        self.adresse = adresse.upper()
        self.entry_id = entry_id
        self.modell = modell if modell in MODELLE else STANDARD_MODELL
        m = MODELLE[self.modell]
        self.name, self.sprache, self.dpi, self.kopf_px = m["name"], m["sprache"], m["dpi"], m["kopf_px"]
        self.info: dict[str, str] = {}
        self.letzter = "noch nichts gedruckt"
        self.ok: bool | None = None
        self.antworten = ""
        self.zeit = ""
        self.verlauf: list[dict] = []
        self.gesamt = 0
        self.fehler = ""
        self._lock = asyncio.Lock()
        self._wartend = 0
        self._store = Store(hass, 1, f"{DOMAIN}.{entry_id}.verlauf")

    # ------------------------------------------------------------ Zustand
    async def async_laden(self) -> None:
        d = await self._store.async_load() or {}
        self.verlauf = d.get("eintraege", [])
        self.gesamt = int(d.get("gesamt", 0))
        self.info = d.get("info", {})

    def _speichern(self) -> None:
        self._store.async_delay_save(
            lambda: {"eintraege": self.verlauf[-200:], "gesamt": self.gesamt, "info": self.info}, 2
        )

    @callback
    def melden(self) -> None:
        async_dispatcher_send(self.hass, f"{SIGNAL_UPDATE}_{self.entry_id}")

    @property
    def erreichbar(self) -> bool:
        return bluetooth.async_address_present(self.hass, self.adresse, connectable=True)

    @property
    def rssi(self) -> int | None:
        si = bluetooth.async_last_service_info(self.hass, self.adresse, connectable=True)
        return si.rssi if si else None

    @property
    def beschaeftigt(self) -> bool:
        return self._lock.locked()

    def zahl(self, k: str) -> int | None:
        try:
            return int(self.info.get(k, ""))
        except ValueError:
            return None

    def zusammenfassung(self) -> dict:
        heute = time.strftime("%Y-%m-%d")
        von_heute = [e for e in self.verlauf if e["zeit"].startswith(heute)]
        return {"heute": sum(1 for e in von_heute if e["ok"]),
                "fehler_heute": sum(1 for e in von_heute if not e["ok"]),
                "gesamt": self.gesamt, "abbrueche_heute": 0,
                "letzte": list(reversed(self.verlauf[-10:]))}

    # ------------------------------------------------------------ Antworten auswerten
    def _auswerten(self, text: str) -> None:
        for z in re.split(r"(?=SSSGET[A-Z]+:)|(?=Cmd error:)|\r\n", text):
            z = z.strip()
            if z.startswith("SSSGET") and ":" in z:
                k, _, v = z[6:].partition(":")
                if k != "PRINTING":
                    self.info[k] = v.strip()
                    self.info["zeit"] = time.strftime("%H:%M:%S")

    # ------------------------------------------------------------ BLE
    async def _sitzung(self, daten: bytes, auf_druck_warten: bool) -> str:
        """Verbinden, Daten senden, Antworten sammeln, trennen."""
        geraet = bluetooth.async_ble_device_from_address(self.hass, self.adresse, connectable=True)
        if geraet is None:
            raise RuntimeError("Drucker nicht in Reichweite eines Bluetooth-Proxys (eingeschaltet? mit Pi/Handy verbunden?)")
        puffer = bytearray()

        def empfangen(_, teil: bytearray) -> None:
            puffer.extend(teil)

        client = await establish_connection(BleakClientWithServiceCache, geraet, self.name, max_attempts=3)
        try:
            try:
                await client.start_notify(UUID_ANTWORT, empfangen)
            except Exception:  # noqa: BLE001 – nicht jedes Modell meldet sich zurück
                if self.sprache == "tspl":
                    raise
            stueck = max(20, min(180, (client.mtu_size or 23) - 3))
            kanal = client.services.get_characteristic(UUID_SCHREIBEN)
            # Mit Bestätigung schreiben, wenn der Drucker das kann – sonst läuft sein Puffer über
            bestaetigt = kanal is None or "write" in kanal.properties

            async def senden(b: bytes) -> None:
                for i in range(0, len(b), stueck):
                    await client.write_gatt_char(UUID_SCHREIBEN, b[i:i + stueck], response=bestaetigt)
                    if not bestaetigt and (i // stueck) % 10 == 9:
                        await asyncio.sleep(0.05)

            await senden(daten)
            if self.sprache == "escpos":
                # Kein Rückkanal „fertig“: Zeit zum Drucken lassen, bevor getrennt wird
                await asyncio.sleep(3 if auf_druck_warten else 0.5)
                return bytes(puffer).hex(" ")[:200]
            if auf_druck_warten:
                ende = time.monotonic() + 40
                gefragt = time.monotonic()
                while time.monotonic() < ende and b"PRINTING:DONE" not in puffer:
                    await asyncio.sleep(0.5)
                    if time.monotonic() - gefragt > 3:
                        gefragt = time.monotonic()
                        await senden(b"SSSGETPRINTING\r\n")
                if b"PRINTING:DONE" not in puffer:
                    raise RuntimeError("Drucker hat das Ende des Drucks nicht gemeldet")
            await senden(INFO_FRAGEN)
            await asyncio.sleep(1.5)
        finally:
            try:
                await client.disconnect()
            except Exception:  # noqa: BLE001
                pass
        text = bytes(puffer).decode("utf-8", "replace")
        self._auswerten(text)
        return text.strip()

    async def async_infos(self) -> None:
        """Rolle, Firmware usw. abfragen (nur M20 Pro – die ESC/POS-Modelle kennen die Fragen nicht)."""
        if self.sprache != "tspl":
            return
        async with self._lock:
            try:
                await self._sitzung(b"", False)
                self.fehler = ""
            except Exception as e:  # noqa: BLE001
                self.fehler = str(e)
                _LOGGER.warning("Infos vom %s nicht abrufbar: %s", self.name, e)
        self._speichern()
        self.melden()

    def auftrag_annehmen(self, auftrag: bytes, titel: str) -> bool:
        """Auftrag im Hintergrund drucken. False, wenn die Warteschlange voll ist."""
        if self._wartend >= WARTESCHLANGE_MAX:
            return False
        self._wartend += 1
        self.hass.async_create_background_task(self._drucken(auftrag, titel), f"{DOMAIN} drucken")
        return True

    async def _drucken(self, auftrag: bytes, titel: str) -> None:
        try:
            async with self._lock:
                self.melden()
                try:
                    vorne = b"SSSGETTYPE\r\n" if self.sprache == "tspl" else b""
                    antworten = await self._sitzung(vorne + auftrag, True)
                    ok, text = True, "%s gedruckt (%d Bytes)" % (titel, len(auftrag))
                    self.fehler = ""
                except Exception as e:  # noqa: BLE001 – Integration soll weiterlaufen
                    ok, text, antworten = False, str(e) or e.__class__.__name__, ""
                    self.fehler = text
                    _LOGGER.warning("Druck auf dem %s fehlgeschlagen: %s", self.name, text)
        finally:
            self._wartend -= 1
        self.verlauf.append({"zeit": time.strftime("%Y-%m-%d %H:%M:%S"), "titel": titel, "ok": ok,
                             "text": "gedruckt" if ok else text})
        if ok:
            self.gesamt += 1
        self.letzter, self.ok, self.antworten, self.zeit = text, ok, antworten, time.strftime("%H:%M:%S")
        self._speichern()
        self.melden()

    # ------------------------------------------------------------ Status wie die Pi-Brücke
    def status(self, version: str) -> dict:
        rssi = self.rssi
        return {
            "version": version, "drucker": self.adresse, "modell": self.modell, "name": self.name,
            "kopf_mm": self.kopf_px // 8, "verbunden": self.erreichbar,
            "verbunden_seit": "", "verbindet": self.beschaeftigt, "naechster_versuch_s": 0,
            "dauerhaft": False, "freigegeben_s": 0, "verbindungsfehler": self.fehler,
            "rolle": {"rest": self.zahl("PAPERCAPACITY"), "gesamt": self.zahl("PAPERTOTALITY"),
                      "rfid": self.info.get("RFID", "")},
            "geraet": {"typ": self.info.get("TYPE", "") or self.name, "firmware": self.info.get("VERSION", ""),
                       "seriennummer": self.info.get("SN", ""), "stand": self.info.get("zeit", "")},
            "akku": {"wert": "", "befehl": ""},
            "signal": {"rssi": rssi, "text": signal_text(rssi), "stand": time.strftime("%H:%M:%S")},
            "drucke": self.zusammenfassung(), "system": {},
            "letzter": self.letzter, "ok": self.ok, "antworten": self.antworten, "zeit": self.zeit,
            "weg": "Home Assistant · Bluetooth LE",
        }


def signal_text(rssi: int | None) -> str:
    """BLE-RSSI in dBm (anders als hcitool am Pi)."""
    if rssi is None:
        return "–"
    return "sehr gut" if rssi >= -60 else "gut" if rssi >= -70 else "mäßig" if rssi >= -80 else "schwach"


AbmeldeFunktion = Callable[[], None]
