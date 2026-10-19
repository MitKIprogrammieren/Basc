"""Minimaler PN532-Treiber für den HSU-Modus (UART) – gleich wie in der Sensor-Firmware.

Liest nur die UID von ISO14443A-Tags. Braucht pyserial (apt: python3-serial).
"""
from __future__ import annotations

import time

TFI_HOST, TFI_PN532 = 0xD4, 0xD5
ACK = bytes([0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00])


class Pn532Hsu:
    def __init__(self, port, baud=115200):
        import serial  # erst hier, damit der Demo-Modus ohne pyserial läuft

        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.bereit = False

    def _aufwecken(self):
        self.ser.write(bytes([0x55, 0x55] + [0] * 14))
        self.ser.flush()
        time.sleep(0.002)

    def _lies(self, n, bis):
        out = bytearray()
        while len(out) < n and time.monotonic() < bis:
            out += self.ser.read(n - len(out))
        return bytes(out)

    def _warte_auf(self, muster, bis):
        pos = 0
        while pos < len(muster):
            b = self._lies(1, bis)
            if not b:
                return False
            if b[0] == muster[pos]:
                pos += 1
            else:
                pos = 1 if b[0] == 0 else 0
        return True

    def befehl(self, daten, timeout=0.1):
        self.ser.reset_input_buffer()
        ln = len(daten) + 1
        summe = (TFI_HOST + sum(daten)) & 0xFF
        rahmen = bytes([0x00, 0x00, 0xFF, ln, (0x100 - ln) & 0xFF, TFI_HOST]) + bytes(daten)
        rahmen += bytes([(0x100 - summe) & 0xFF, 0x00])
        self.ser.write(rahmen)
        self.ser.flush()
        if not self._warte_auf(ACK, time.monotonic() + 0.03):
            return None
        bis = time.monotonic() + timeout
        if not self._warte_auf(bytes([0x00, 0x00, 0xFF]), bis):
            return None
        kopf = self._lies(2, bis)
        if len(kopf) < 2 or (kopf[0] + kopf[1]) & 0xFF or kopf[0] < 2:
            return None
        rest = self._lies(kopf[0] + 2, bis)  # TFI CMD DATEN… DCS 00
        if len(rest) < kopf[0] + 1 or rest[0] != TFI_PN532 or rest[1] != daten[0] + 1:
            return None
        if sum(rest[: kopf[0] + 1]) & 0xFF:
            return None
        return rest[2: kopf[0]]

    def begin(self):
        self._aufwecken()
        if self.befehl([0x02]) is None:
            self._aufwecken()
            if self.befehl([0x02]) is None:
                return False
        if self.befehl([0x14, 0x01, 0x14, 0x01]) is None:
            return False
        self.befehl([0x32, 0x05, 0xFF, 0x01, 0x02])
        self.bereit = True
        return True

    def lese_uid(self):
        if not self.bereit:
            return ""
        a = self.befehl([0x4A, 0x01, 0x00], timeout=0.08)
        if not a or len(a) < 6 or a[0] == 0:
            return ""
        n = a[5]
        if n == 0 or 6 + n > len(a):
            return ""
        return a[6: 6 + n].hex().upper()
