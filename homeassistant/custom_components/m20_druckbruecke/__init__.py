"""Druckbrücke – druckt Etiketten der FilamentLager-Waage per Bluetooth LE über Home Assistant (M20 Pro, M110 …)."""
from __future__ import annotations

import time

from homeassistant.components import bluetooth
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_call_later

from .const import CONF_ADDRESS, CONF_MODELL, DOMAIN, STANDARD_MODELL, modell_aus_name
from .drucker import M20Drucker
from .http import DruckView

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR, Platform.BUTTON]
VERSION = "1.3.0"


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    modell = entry.data.get(CONF_MODELL) or modell_aus_name(entry.title) or STANDARD_MODELL
    drucker = M20Drucker(hass, entry.data[CONF_ADDRESS], entry.entry_id, modell)
    await drucker.async_laden()
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = drucker

    if not hass.data.get(f"{DOMAIN}_view"):
        hass.http.register_view(DruckView(hass, VERSION))
        hass.data[f"{DOMAIN}_view"] = True

    zuletzt = {"rssi": None, "zeit": 0.0}

    @callback
    def gesehen(info: bluetooth.BluetoothServiceInfoBleak, _change: bluetooth.BluetoothChange) -> None:
        # Signal höchstens alle 30 s melden, damit der Recorder nicht vollläuft
        if time.monotonic() - zuletzt["zeit"] < 30 and zuletzt["rssi"] is not None:
            return
        zuletzt["rssi"], zuletzt["zeit"] = info.rssi, time.monotonic()
        drucker.melden()

    @callback
    def weg(_info: bluetooth.BluetoothServiceInfoBleak) -> None:
        zuletzt["rssi"] = None
        drucker.melden()

    entry.async_on_unload(bluetooth.async_register_callback(
        hass, gesehen, bluetooth.BluetoothCallbackMatcher(address=drucker.adresse, connectable=True),
        bluetooth.BluetoothScanningMode.PASSIVE))
    entry.async_on_unload(bluetooth.async_track_unavailable(hass, weg, drucker.adresse, connectable=True))

    async def erste_abfrage(_now) -> None:
        if drucker.erreichbar and not drucker.beschaeftigt:
            await drucker.async_infos()

    entry.async_on_unload(async_call_later(hass, 30, erste_abfrage))

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if ok:
        hass.data[DOMAIN].pop(entry.entry_id, None)
    return ok
