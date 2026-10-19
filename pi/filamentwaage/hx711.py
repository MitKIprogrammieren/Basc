"""HX711 direkt an den GPIOs des Raspberry Pi (über lgpio, apt: python3-lgpio).

Der HX711 ist zeitkritisch: SCK darf nicht länger als 60 µs HIGH bleiben.
Unter Linux klappt das meistens; einzelne Ausreißer filtert der Median weg.
Für die zuverlässigste Messung lieber die Sensorplatine (ESP32-S3-Zero) per USB nutzen.
"""
from __future__ import annotations

import time


class Hx711:
    def __init__(self, dout, sck, chip=0):
        import lgpio

        self.lg = lgpio
        self.h = lgpio.gpiochip_open(chip)
        self.dout, self.sck = dout, sck
        lgpio.gpio_claim_input(self.h, dout)
        lgpio.gpio_claim_output(self.h, sck, 0)

    def bereit(self):
        return self.lg.gpio_read(self.h, self.dout) == 0

    def lesen(self):
        """Ein Rohwert (24 Bit, Kanal A, Verstärkung 128) oder None."""
        bis = time.monotonic() + 0.2
        while not self.bereit():
            if time.monotonic() > bis:
                return None
            time.sleep(0.001)
        w, r = self.lg.gpio_write, self.lg.gpio_read
        wert = 0
        for _ in range(24):
            w(self.h, self.sck, 1)
            w(self.h, self.sck, 0)
            wert = (wert << 1) | r(self.h, self.dout)
        w(self.h, self.sck, 1)  # 25. Takt: Kanal A, 128
        w(self.h, self.sck, 0)
        if wert & 0x800000:
            wert -= 0x1000000
        return wert
