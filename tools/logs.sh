#!/bin/sh
# Follow the recorder's log over Wi-Fi (like `esphome logs`). Ctrl-C to stop.
#   tools/logs.sh                 # http://pianolog.local
#   tools/logs.sh 192.168.4.1     # while on the recorder's own network
#   tools/logs.sh pianolog.local once   # print what is buffered and exit
# Survives restarts: after an update or crash it picks up the new boot by itself.
HOST=${1:-pianolog.local}
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM
next=0
while :; do
    if curl -s -m 5 -D "$TMP/h" -o "$TMP/b" "http://$HOST/api/log?since=$next"; then
        cat "$TMP/b"
        n=$(tr -d '\r' < "$TMP/h" | sed -n 's/^[Xx]-[Ll]og-[Nn]ext: *\([0-9]*\).*/\1/p')
        [ -n "$n" ] && next=$n
        [ "$2" = once ] && exit 0
    fi
    sleep 1
done
