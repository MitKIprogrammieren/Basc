"""Sensoren: Etikettenrolle, Signal, Drucke, letzter Druck, Firmware."""
from __future__ import annotations

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import SIGNAL_STRENGTH_DECIBELS_MILLIWATT, EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .entity import M20Entity


class Rolle(M20Entity, SensorEntity):
    _attr_icon = "mdi:label-multiple-outline"
    _attr_native_unit_of_measurement = "Etiketten"
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, d):
        super().__init__(d, "rolle", "Etiketten übrig")

    @property
    def native_value(self):
        return self.drucker.zahl("PAPERCAPACITY")

    @property
    def extra_state_attributes(self):
        return {"rollengroesse": self.drucker.zahl("PAPERTOTALITY"), "rfid": self.drucker.info.get("RFID", "")}


class Signal(M20Entity, SensorEntity):
    _attr_device_class = SensorDeviceClass.SIGNAL_STRENGTH
    _attr_native_unit_of_measurement = SIGNAL_STRENGTH_DECIBELS_MILLIWATT
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, d):
        super().__init__(d, "signal", "Funksignal")

    @property
    def native_value(self):
        return self.drucker.rssi


class DruckeHeute(M20Entity, SensorEntity):
    _attr_icon = "mdi:printer-check"
    _attr_native_unit_of_measurement = "Drucke"

    def __init__(self, d):
        super().__init__(d, "heute", "Drucke heute")

    @property
    def native_value(self):
        return self.drucker.zusammenfassung()["heute"]


class DruckeGesamt(M20Entity, SensorEntity):
    _attr_icon = "mdi:counter"
    _attr_native_unit_of_measurement = "Drucke"
    _attr_state_class = SensorStateClass.TOTAL_INCREASING

    def __init__(self, d):
        super().__init__(d, "gesamt", "Drucke gesamt")

    @property
    def native_value(self):
        return self.drucker.gesamt


class LetzterDruck(M20Entity, SensorEntity):
    _attr_icon = "mdi:label-outline"

    def __init__(self, d):
        super().__init__(d, "letzter", "Letzter Druck")

    @property
    def native_value(self):
        if self.drucker.beschaeftigt:
            return "druckt …"
        if self.drucker.ok is None:
            return "noch nichts gedruckt"
        return ("ok · " if self.drucker.ok else "Fehler · ") + self.drucker.zeit

    @property
    def extra_state_attributes(self):
        return {"meldung": self.drucker.letzter, "fehler": self.drucker.fehler,
                "letzte": self.drucker.zusammenfassung()["letzte"][:5]}


class Firmware(M20Entity, SensorEntity):
    _attr_icon = "mdi:chip"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, d):
        super().__init__(d, "firmware", "Firmware")

    @property
    def native_value(self):
        return self.drucker.info.get("VERSION") or None

    @property
    def extra_state_attributes(self):
        return {"seriennummer": self.drucker.info.get("SN", ""), "typ": self.drucker.info.get("TYPE", ""),
                "abgefragt": self.drucker.info.get("zeit", "")}


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, add: AddEntitiesCallback) -> None:
    d = hass.data[DOMAIN][entry.entry_id]
    if d.sprache == "tspl":  # Rolle und Firmware meldet nur der M20 Pro
        add([Rolle(d), Signal(d), DruckeHeute(d), DruckeGesamt(d), LetzterDruck(d), Firmware(d)])
    else:
        add([Signal(d), DruckeHeute(d), DruckeGesamt(d), LetzterDruck(d)])
