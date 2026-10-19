#!/usr/bin/env bash
# Baut aus dem BTT-CB1-Klipper-Image ein FilamentLager-Pad7-Image (als root, braucht Loop-Geräte).
#   sudo PYTHONPATH=/pfad/zu/pyfatfs-und-abhaengigkeiten tools/pad7_image.sh CB1_…_Klipper_….img FilamentLager-Pad7.img
#
#  - BOOT (FAT): Ordner filamentwaage (Programm, Installer, config.pad7.toml), hostname in system.cfg.
#    Geschrieben mit pyfatfs, damit kein vfat-Treiber nötig ist.
#  - System (ext4): Ersteinrichtungs-Dienst; KlipperScreen/Klipper/Moonraker abgeschaltet,
#    damit der Bildschirm beim ersten Start den Fortschritt zeigt.
# Beim ersten Start lädt das Pad 7 Chromium aus dem Internet und richtet sich ein.
set -euo pipefail
WURZEL="$(cd "$(dirname "$0")/.." && pwd)"
EIN="$1"; AUS="$2"
[ "$(id -u)" = 0 ] || { echo "als root starten"; exit 1; }

cp --sparse=always "$EIN" "$AUS"

# Partitionen aus der MBR-Tabelle: FAT (0x0b/0x0c/0x0e/0xea…) und Linux (0x83)
read -r FAT_OFF EXT_OFF EXT_LEN < <(python3 - "$AUS" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read(512)
fat = ext = None
for i in range(4):
    e = d[446 + 16 * i: 462 + 16 * i]
    typ = e[4]; start, anz = struct.unpack("<II", e[8:16])
    if not anz: continue
    if typ == 0x83 and ext is None: ext = (start * 512, anz * 512)
    elif fat is None: fat = start * 512
print(fat, ext[0], ext[1])
EOF
)
echo "== BOOT bei $FAT_OFF, System bei $EXT_OFF"

M="$(mktemp -d)"
LOOP=""
aufraeumen() { umount -q "$M" 2>/dev/null || true; [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null || true; rmdir "$M" 2>/dev/null || true; }
trap aufraeumen EXIT

echo "== BOOT: Ordner filamentwaage und hostname"
ZIP="$(mktemp -u).zip"; TMP="$(mktemp -d)"
"$WURZEL/tools/pad7_paket.sh" "$ZIP" >/dev/null
unzip -q "$ZIP" -d "$TMP"
python3 "$WURZEL/tools/fat_schreiben.py" "$AUS" "$FAT_OFF" "$TMP/filamentwaage"
python3 "$WURZEL/tools/fat_schreiben.py" "$AUS" "$FAT_OFF" --hostname FilamentLager-Pad7
rm -rf "$ZIP" "$TMP"

echo "== System: Ersteinrichtung, Klipper-Dienste aus"
LOOP="$(losetup -f --show -o "$EXT_OFF" --sizelimit "$EXT_LEN" "$AUS")"
mount "$LOOP" "$M"
install -d "$M/opt/filamentwaage"
install -m 755 "$WURZEL/pi/pad7/erststart.sh" "$M/opt/filamentwaage/erststart.sh"
install -m 644 "$WURZEL/pi/pad7/filamentwaage-erststart.service" "$M/etc/systemd/system/filamentwaage-erststart.service"
ln -sf /etc/systemd/system/filamentwaage-erststart.service "$M/etc/systemd/system/multi-user.target.wants/filamentwaage-erststart.service"
for d in KlipperScreen klipper moonraker crowsnest klipper-mcu sonar; do
  rm -f "$M/etc/systemd/system/multi-user.target.wants/$d.service"
done
sync
umount "$M"; losetup -d "$LOOP"; LOOP=""
echo "== fertig: $AUS"
