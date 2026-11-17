#!/bin/sh
# Send a firmware image to the recorder over Wi-Fi.
#   tools/ota.sh                         # build/pianolog.bin to http://pianolog.local
#   tools/ota.sh firmware/pianolog-ota-rev3.bin 192.168.4.1
# The token is read from PIANOLOG_OTA_TOKEN, or from sdkconfig (CONFIG_PL_OTA_TOKEN).
set -e
IMG=${1:-build/pianolog.bin}
HOST=${2:-pianolog.local}
TOKEN=${PIANOLOG_OTA_TOKEN:-$(sed -n 's/^CONFIG_PL_OTA_TOKEN="\(.*\)"$/\1/p' sdkconfig 2>/dev/null)}
[ -n "$TOKEN" ] || { echo "Set PIANOLOG_OTA_TOKEN or build once so sdkconfig exists." >&2; exit 1; }
[ -f "$IMG" ] || { echo "No image at $IMG (run idf.py build first)." >&2; exit 1; }
echo "Uploading $IMG ($(wc -c < "$IMG") bytes) to $HOST ..."
curl --fail-with-body -sS --max-time 300 -H "X-Update-Token: $TOKEN" \
     -H "Content-Type: application/octet-stream" --data-binary @"$IMG" "http://$HOST/api/update"
