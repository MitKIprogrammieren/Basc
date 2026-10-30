"""Gemeinsame Basis der Entitäten."""
from __future__ import annotations

from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity import Entity

from .const import DOMAIN, SIGNAL_UPDATE
from .drucker import M20Drucker


class M20Entity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, drucker: M20Drucker, schluessel: str, name: str) -> None:
        self.drucker = drucker
        self._attr_name = name
        self._attr_unique_id = f"{drucker.adresse}_{schluessel}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, drucker.adresse)},
            connections={(dr.CONNECTION_BLUETOOTH, drucker.adresse)},
            name=f"Phomemo {drucker.name}",
            manufacturer="Phomemo",
            model=drucker.name,
            sw_version=drucker.info.get("VERSION") or None,
        )

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(async_dispatcher_connect(
            self.hass, f"{SIGNAL_UPDATE}_{self.drucker.entry_id}", self.async_write_ha_state))
