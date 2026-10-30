"""Knöpfe: Testdruck und Druckerinfos abfragen."""
from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN, TEST
from .escpos import escpos_test
from .entity import M20Entity


class Testdruck(M20Entity, ButtonEntity):
    _attr_icon = "mdi:printer"

    def __init__(self, d):
        super().__init__(d, "testdruck", "Testdruck")

    async def async_press(self) -> None:
        test = TEST if self.drucker.sprache == "tspl" else escpos_test(self.drucker.kopf_px)
        if not self.drucker.auftrag_annehmen(test, "Testdruck"):
            raise HomeAssistantError("Drucker ist noch beschäftigt")


class Infos(M20Entity, ButtonEntity):
    _attr_icon = "mdi:refresh"

    def __init__(self, d):
        super().__init__(d, "infos", "Infos abfragen")

    async def async_press(self) -> None:
        await self.drucker.async_infos()


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, add: AddEntitiesCallback) -> None:
    d = hass.data[DOMAIN][entry.entry_id]
    add([Testdruck(d), Infos(d)] if d.sprache == "tspl" else [Testdruck(d)])
