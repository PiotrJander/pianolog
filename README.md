# pianolog

[![build](https://github.com/PiotrJander/pianolog/actions/workflows/build.yml/badge.svg)](https://github.com/PiotrJander/pianolog/actions/workflows/build.yml)

An always-on MIDI recorder for digital pianos, built on a Waveshare ESP32-P4-NANO board.
Plug it into the piano and forget it: every take is saved as a Standard MIDI File, first in
the board's own flash and then on a microSD card, and a small web page served by the device
lets you browse, play and download takes from a phone. After the first flash by cable,
updates go over Wi-Fi.

* **Build manual** (parts, wiring, flashing, troubleshooting):
  [piotrjander.github.io/pianolog](https://piotrjander.github.io/pianolog/), source in
  [`docs/index.html`](docs/index.html)
* **Try the web interface** with a few example takes, no hardware needed:
  [piotrjander.github.io/pianolog/demo.html](https://piotrjander.github.io/pianolog/demo.html)
* **MIDI tester**: plays tunes and edge-case sequences (take gaps, pedal, 16 channels, SysEx,
  bursts, stuck notes, power-cut take) into a MIDI output of your computer, with the expected
  result for each: [piotrjander.github.io/pianolog/midi-tester.html](https://piotrjander.github.io/pianolog/midi-tester.html).
  A laptop can't plug straight into the recorder (both are USB hosts); two USB-MIDI cables
  joined by a DIN coupler make the bridge.

> **Status: compiles and passes tests on a PC, but has never run on real hardware.**
> Expect a first round of fixes once a board is on the bench. See [What is tested](#what-is-tested).

## Inspiration

The idea comes from [Jamcorder](https://www.jamcorder.com/) by Chip Weinberger, a commercial
"set and forget" piano recorder ([Show HN](https://news.ycombinator.com/item?id=42082430)).
pianolog is an independent DIY take on the same idea, written from scratch and not affiliated
with Jamcorder. If you want a polished device with a phone app, Bluetooth sync, MIDI
playback through the piano and a proper enclosure, buy a Jamcorder.

## Parts

| # | Part | Notes | Approx. price |
|---|------|-------|---------------|
| 1 | [Waveshare ESP32-P4-NANO](https://kamami.pl/en/esp32/1197470-esp32-p4-nano-high-performance-development-board-based-on-esp32-p4-chip-with-risc-v-dual-core-and-s-5902186307353.html) (SKU 29026) | ESP32-P4 with USB host on a Type-A socket, microSD slot, ESP32-C6 for Wi-Fi with an on-board antenna. Get the basic version, not the `-WIFI6-DB` one with an external antenna | 85 zł / ~20 € |
| 2 | microSD card, 8–32 GB, FAT32 | **Optional.** Without it the board keeps ~50 h of playing and overwrites the oldest takes | 15–30 zł |
| 3 | USB-A to USB-C cable + any 5 V / 1 A USB charger | Power, and the first flash from a computer | 5 zł + charger |
| 4a | USB-A to USB-B cable | For a piano with a square **USB TO HOST** socket (most current models) | 10–15 zł |
| 4b | USB-A to USB-C *data* cable | For a piano with a USB-C socket. Charge-only cables won't work | 5 zł |
| 4c | Class-compliant USB-MIDI interface cable (e.g. Roland UM-ONE mk2) | For an instrument with only round 5-pin MIDI sockets | 60–180 zł |

You need one of 4a/4b/4c. No soldering. The links and prices are from a Polish shop
(Kamami, Waveshare's distributor there) in November 2026; the parts are generic and available
everywhere. A 3D-printed case is not designed yet.

## Hardware

| Function            | Connection on the ESP32-P4-NANO                                    |
|---------------------|--------------------------------------------------------------------|
| Piano (USB)         | Type-A port (ESP32-P4 high-speed USB host)                         |
| Piano (5-pin DIN)   | H11L1 optocoupler output -> GPIO20 (UART1 RX, 31250 baud), optional |
| Storage             | 16 MB on-board flash (about 9.9 MB for takes) + microSD, FAT32     |
| Wi-Fi               | on-board ESP32-C6 over SDIO (GPIO14-19, reset GPIO54), esp_hosted  |
| Power, flashing     | USB-C port                                                         |

Both inputs are recorded into the same take. A DIN-only instrument also works without any
soldering through a class-compliant USB-MIDI cable plugged into the Type-A port.

## Before the first build: secrets

Edit these lines in `sdkconfig.defaults` (or set them in `idf.py menuconfig` -> `pianolog`):

```
CONFIG_PL_WIFI_SSID="YOUR_WIFI_SSID"        # built-in home network (placeholder = none)
CONFIG_PL_WIFI_PASS="YOUR_WIFI_PASSWORD"
CONFIG_PL_AP_PASS="pianolog-setup"          # the recorder's own setup network, 8+ characters
CONFIG_PL_OTA_TOKEN="CHANGE-ME-ota-token"   # secret for updates over Wi-Fi; empty disables them
```

Don't commit real values. The prebuilt images in `firmware/` contain exactly these
placeholders, so anyone who reads this README knows their update token: build your own image
before the recorder lives on a network you share.

## Build and flash

Needs ESP-IDF **5.5.3 or newer in the 5.5 line** (developed against 5.5.6).

The first flash must be by cable through the USB-C port. It also writes the partition table
(two firmware slots plus the recordings area), which updates over Wi-Fi cannot change.

```sh
idf.py set-target esp32p4
idf.py build flash monitor
```

ESP32-P4 chips before and after revision 3.0 need different binaries. The default build is
for v3.0 and newer (chip marked `P4NRW32X`). If the boot log or `esptool chip-id` reports a
revision below 3.0, rebuild from scratch with the extra defaults file:

```sh
rm -rf build sdkconfig
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1" set-target esp32p4
idf.py build flash monitor
```

Prebuilt images are in [`firmware/`](firmware/): `pianolog-p4-rev3.bin` / `-rev1.bin` are full
images for the first flash by cable (offset 0); `pianolog-ota-rev3.bin` / `-rev1.bin` are the
app images for updates over Wi-Fi. For the first flash:

```sh
python -m esptool --chip esp32p4 -b 460800 write-flash 0x0 firmware/pianolog-p4-rev3.bin
```

Board-specific options (SD power, DIN pin) are under `idf.py menuconfig` -> `pianolog`.

## Updates over Wi-Fi

```sh
idf.py build && tools/ota.sh                      # -> http://pianolog.local
tools/ota.sh build/pianolog.bin 192.168.4.1       # while on the recorder's own network
```

`tools/ota.sh` sends the app image (`build/pianolog.bin`, not the merged one) with the token
from `sdkconfig` in the `X-Update-Token` header. Equivalent:

```sh
curl -H "X-Update-Token: <token>" --data-binary @build/pianolog.bin http://pianolog.local/api/update
```

* The image goes into the inactive slot and is checked (format, chip, SHA-256) before the
  recorder switches to it. A take being recorded is closed cleanly before the restart.
* **Rollback:** the new firmware counts as good only once the flash is mounted and the
  network is up (home Wi-Fi or its own setup network) within 120 s. If it crashes or never gets
  there, the bootloader goes back to the previous version by itself.
* The token travels in clear text over HTTP. It keeps out mistakes and strangers, not someone
  sniffing your Wi-Fi. Images are not signed.
* Laptop and recorder must be on the same network, and the network must let devices talk to
  each other. Home networks do; guest, hotel and office networks often isolate clients.
* Use the image built for your chip revision; a wrong one is rejected or rolled back.

## Settings and Wi-Fi

Networks are tried in this order, two attempts each:

1. the network saved from the setup page (kept in the board's NVS settings storage)
2. `wifi_ssid` / `wifi_pass` from `pianolog.txt` on the card
3. `CONFIG_PL_WIFI_SSID` / `CONFIG_PL_WIFI_PASS` built into the firmware

If none connects within 30 s, or none is set, the recorder opens its own network, named after
`hostname` and protected by `ap_pass` (default: the built-in `CONFIG_PL_AP_PASS`). Join it and
open `http://192.168.4.1/setup` to pick a network from a scan and type its password. Changing
the Wi-Fi works only from that network, so nobody on your home network can move the recorder
to another router. Once a home network is joined, the setup network closes. 2.4 GHz only.

On first boot with a card, the recorder writes `pianolog.txt` to it:

```
wifi_ssid=MyNetwork
wifi_pass=secret
ap_pass=
hostname=pianolog
tz=CET-1CEST,M3.5.0,M10.5.0/3
gap_s=60
```

The clock comes from NTP, or from the browser when there is no internet.

## How takes are stored

Every take is recorded into the board's own flash (LittleFS file system, about 9.9 MB), never
straight to the card:

```
flash  /rec/2026-11-17/20-58-12.mid                     being recorded (or cut by power loss)
flash  /rec/2026-11-17/20-31-05_0185s_0412n.mid         closed, waiting to be moved
card   /rec/2026-11-13/19-02-44_0024s_0132n_star.mid    archived, starred from the web page
card   /rec/unsynced/b0042-001_0090s_0200n.mid          recorded before the clock was set
```

* A take starts at the first Note On and ends after `gap_s` seconds without events.
* Format 0, 1000 ticks per quarter at 60 BPM, so one tick is one millisecond. The file is
  served byte for byte on download; the page's "Download" gives exactly what is stored.
* Recorded: all channel messages on all 16 channels and SysEx up to 1 kB.
  Dropped: real-time and system-common messages (clock, active sensing).
* **Power cuts.** The take is synced every 2 s; at most the last two seconds are lost, and an
  interrupted take is completed at the next boot. LittleFS never overwrites data in place, so
  a cut cannot corrupt the file system itself. That was the weak spot of version 0.1, which
  wrote to the card's FAT (no journal) every two seconds.
* **Moving to the card.** After a take closes, it is copied to the card, read back and compared,
  renamed into place, and only then deleted from flash. A cut at any step leaves at least one
  complete copy; the next pass cleans up. The card is written only in short bursts.
* **No card.** Takes stay in flash, roughly 50+ hours of dense playing. When less than 512 kB is
  left, the oldest takes are deleted until 1 MB is free; starred takes only when nothing
  else is left, and never the take being recorded. With a card in, nothing is deleted.
  Insert a card later and everything in flash is moved over.

## HTTP API

| Method | Path                                   | Result                               |
|--------|----------------------------------------|--------------------------------------|
| GET    | `/`                                    | the web page                         |
| GET    | `/api/status`                          | recorder, inputs, card, network      |
| GET    | `/api/days`                            | `["2026-11-13", ...]` newest first   |
| GET    | `/api/list?day=2026-11-13`             | takes of that day                    |
| GET    | `/rec/<day>/<file>.mid`                | the MIDI file                        |
| POST   | `/api/star?day=&f=&on=1`               | renames the take, returns new name   |
| POST   | `/api/delete?day=&f=`                  | deletes the take                     |
| POST   | `/api/time?t=<unix seconds>`           | sets the clock if it is not set      |
| GET    | `/setup`                               | Wi-Fi setup page                     |
| GET    | `/api/scan`                            | nearby networks (setup network only) |
| POST   | `/api/wifi` (form: `ssid`, `pass`)     | saves a network (setup network only) |
| POST   | `/api/update` + `X-Update-Token`       | firmware update, then restart        |

Takes listed by `/api/list` carry `"where":"flash"` or `"where":"card"`.
Apart from the update token, there is no authentication: anyone on the same network can
read and delete takes.

## Source layout

```
main/main.c        start-up order
main/midi_usb.c    USB host client for MIDI Streaming devices
main/midi_din.c    UART input for the optocoupler
main/midi_parse.*  MIDI byte-stream and USB-MIDI packet parser   (portable, host-tested)
main/smf.*         MIDI file encoder and crash-repair scanner    (portable, host-tested)
main/recorder.c    take detection, safe writing, repair
main/archive.c     flash -> card mover, ring buffer without a card
main/storage.c     on-board flash (LittleFS) and microSD
main/ota.c         updates over Wi-Fi, rollback guard
main/net.c         Wi-Fi station / fallback access point, NTP, mDNS
main/web.c         HTTP server and API
main/www/index.html the whole web UI: list, piano roll, WebAudio synth
main/www/setup.html Wi-Fi setup page
tools/ota.sh       sends a build over Wi-Fi
test/host_test.c   unit tests: gcc -I../main host_test.c ../main/smf.c ../main/midi_parse.c
test/archive_test.c archiver tests (see the comment at its top for how to build)
```

## What is tested

Done on a PC:

* Both firmware variants compile and link with ESP-IDF 5.5.6 for `esp32p4`, rollback enabled.
  GitHub Actions rebuilds both variants and runs the host tests on every push; the images
  are attached to each run as artifacts.
* `test/host_test.c`: encoder, parser (running status, interleaved real-time bytes, SysEx,
  USB packets) and the repair scanner against every possible truncation point; the output
  files were read back with the independent `mido` library.
* `test/archive_test.c`: moving takes to a card, recovery from a cut before and after the
  rename, a name clash, and the ring buffer (oldest first, starred last, live take kept,
  disabled with a card).
* The web pages in headless Chrome at phone size against a mock of the API: listing,
  playback, starring, the Wi-Fi setup form with awkward names and passwords.

Not tested, because no board was available:

* Everything that touches the hardware: USB enumeration of a real piano, 5 V on the Type-A
  port, the SD power assumptions (on-chip LDO channel 4 plus GPIO45 driven low), the link to
  the ESP32-C6, scanning while the setup network is up, an actual update and rollback.
* If the factory C6 firmware is much older than the esp_hosted host driver pulled in by this
  build, Wi-Fi may not start until the C6 is updated (see the esp_hosted documentation).
  Recording does not depend on it, but a freshly updated image that cannot bring up Wi-Fi
  is rolled back after 120 s by design.
* Timing accuracy, long-run stability, card removal while running.

## Known limits

* One USB MIDI device at a time; USB hubs are not enabled.
* No MIDI out / playback through the piano, no Bluetooth, no phone app.
* The piano must be class-compliant USB MIDI (practically all current digital pianos are).

## License

MIT, see [LICENSE](LICENSE).
