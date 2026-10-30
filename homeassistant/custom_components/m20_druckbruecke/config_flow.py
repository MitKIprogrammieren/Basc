"""Einrichtung: automatisch über Bluetooth gefunden oder Adresse von Hand – je Drucker ein Eintrag."""
from __future__ import annotations

import voluptuous as vol

from homeassistant.components.bluetooth import BluetoothServiceInfoBleak, async_discovered_service_info
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult

from .const import CONF_ADDRESS, CONF_MODELL, DOMAIN, MODELLE, STANDARD_ADRESSE, modell_aus_name


class M20ConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._gefunden: BluetoothServiceInfoBleak | None = None
        self._modell: str | None = None

    async def async_step_bluetooth(self, discovery_info: BluetoothServiceInfoBleak) -> ConfigFlowResult:
        modell = modell_aus_name(discovery_info.name)
        if modell is None:
            return self.async_abort(reason="unbekannt")
        await self.async_set_unique_id(discovery_info.address.upper())
        self._abort_if_unique_id_configured()
        self._gefunden, self._modell = discovery_info, modell
        self.context["title_placeholders"] = {"name": discovery_info.name}
        return await self.async_step_bestaetigen()

    async def async_step_bestaetigen(self, user_input=None) -> ConfigFlowResult:
        assert self._gefunden and self._modell
        if user_input is not None:
            return self.async_create_entry(title=self._gefunden.name, data={
                CONF_ADDRESS: self._gefunden.address.upper(), CONF_MODELL: self._modell})
        self._set_confirm_only()
        return self.async_show_form(step_id="bestaetigen",
                                    description_placeholders={"name": self._gefunden.name})

    async def async_step_user(self, user_input=None) -> ConfigFlowResult:
        errors = {}
        if user_input is not None:
            adresse = user_input[CONF_ADDRESS].strip().upper()
            modell = user_input[CONF_MODELL]
            if len(adresse) != 17 or adresse.count(":") != 5:
                errors[CONF_ADDRESS] = "adresse"
            else:
                await self.async_set_unique_id(adresse)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(title=MODELLE[modell]["name"],
                                               data={CONF_ADDRESS: adresse, CONF_MODELL: modell})
        # Vorschlag: ein gefundener Phomemo, der noch nicht eingerichtet ist
        schon = {e.unique_id for e in self._async_current_entries()}
        vorschlag, v_modell = STANDARD_ADRESSE if STANDARD_ADRESSE not in schon else "", "m20pro"
        for info in async_discovered_service_info(self.hass, connectable=True):
            m = modell_aus_name(info.name)
            if m and info.address.upper() not in schon:
                vorschlag, v_modell = info.address.upper(), m
                break
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({
                vol.Required(CONF_ADDRESS, default=vorschlag): str,
                vol.Required(CONF_MODELL, default=v_modell): vol.In({k: v["name"] for k, v in MODELLE.items()}),
            }),
            errors=errors,
        )
