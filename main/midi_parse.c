#include "midi_parse.h"
#include <string.h>

void midi_parser_init(midi_parser_t *p, midi_msg_fn cb, void *ctx)
{
    memset(p, 0, sizeof *p);
    p->cb = cb;
    p->ctx = ctx;
}

static int data_len(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    default: return 0;
    }
}

void midi_parser_feed(midi_parser_t *p, int64_t t_us, const uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = bytes[i];

        if (b >= 0xF8) continue;                // real-time (clock, active sensing...): never recorded

        if (b & 0x80) {                         // status byte
            if (p->in_sysex) {                  // any status ends a SysEx; only F7 ends it cleanly
                if (b == 0xF7 && !p->sysex_overflow && p->sx_len < MIDI_SYSEX_MAX) {
                    p->sx[p->sx_len++] = 0xF7;
                    p->cb(p->ctx, t_us, p->sx, p->sx_len);
                }
                p->in_sysex = false;
                if (b == 0xF7) continue;
            }
            if (b == 0xF0) {
                p->in_sysex = true;
                p->sysex_overflow = false;
                p->sx[0] = 0xF0;
                p->sx_len = 1;
                p->status = 0;
            } else if (b >= 0xF0) {             // system common (F1-F7): cancels running status, not recorded
                p->status = 0;
            } else {
                p->status = b;
            }
            p->have = 0;
            continue;
        }

        // data byte
        if (p->in_sysex) {
            if (p->sx_len < MIDI_SYSEX_MAX - 1) p->sx[p->sx_len++] = b;
            else p->sysex_overflow = true;
            continue;
        }
        if (!p->status) continue;               // stray data (e.g. operand of a system common message)
        int need = data_len(p->status);
        if (p->have == 0 && need == 2) {
            p->d1 = b;
            p->have = 1;
            continue;
        }
        uint8_t msg[3] = { p->status, need == 2 ? p->d1 : b, b };
        p->have = 0;                            // running status stays armed
        p->cb(p->ctx, t_us, msg, (size_t)need + 1);
    }
}

void midi_parser_feed_usb(midi_parser_t *p, int64_t t_us, const uint8_t *pkts, size_t n)
{
    // Bytes carried by each Code Index Number (USB-MIDI 1.0, table 4-1). 0 = reserved/ignored.
    static const uint8_t cin_len[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint8_t len = cin_len[pkts[i] & 0x0F];
        if (len) midi_parser_feed(p, t_us, &pkts[i + 1], len);
    }
}
