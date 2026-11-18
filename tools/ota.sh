#!/bin/sh
# Send a firmware image to the recorder over Wi-Fi.
#   tools/ota.sh                         # build/pianolog.bin to http://pianolog.local
#   tools/ota.sh firmware/pianolog-ota-rev3.bin 192.168.4.1
#   tools/ota.sh firmware/wifi-c6-esp-hosted-2.13.1.bin   # Wi-Fi chip (ESP32-C6) image
# The token is read from PIANOLOG_OTA_TOKEN, or from sdkconfig (CONFIG_PL_OTA_TOKEN).
# An ESP32-C6 image (chip id 0x000d in its header) goes to /api/update-wifi, anything else
# to /api/update.
set -e
IMG=${1:-build/pianolog.bin}
HOST=${2:-pianolog.local}
TOKEN=${PIANOLOG_OTA_TOKEN:-$(sed -n 's/^CONFIG_PL_OTA_TOKEN="\(.*\)"$/\1/p' sdkconfig 2>/dev/null)}
[ -n "$TOKEN" ] || { echo "Set PIANOLOG_OTA_TOKEN or build once so sdkconfig exists." >&2; exit 1; }
[ -f "$IMG" ] || { echo "No image at $IMG (run idf.py build first)." >&2; exit 1; }
CHIP=$(od -An -tx1 -j12 -N2 "$IMG" | tr -d ' \n')
PATH_=/api/update
[ "$CHIP" = 0d00 ] && PATH_=/api/update-wifi
echo "Uploading $IMG ($(wc -c < "$IMG") bytes) to $HOST$PATH_ ..."
curl --fail-with-body -sS --max-time 300 -H "X-Update-Token: $TOKEN" \
     -H "Content-Type: application/octet-stream" --data-binary @"$IMG" "http://$HOST$PATH_"
