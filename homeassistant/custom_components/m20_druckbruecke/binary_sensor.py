"""Erreichbarkeit des Druckers über einen Bluetooth-Proxy."""
from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .entity import M20Entity


class Erreichbar(M20Entity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(self, d):
        super().__init__(d, "erreichbar", "Erreichbar")

    @property
    def is_on(self):
        return self.drucker.erreichbar


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, add: AddEntitiesCallback) -> None:
    add([Erreichbar(hass.data[DOMAIN][entry.entry_id])])
