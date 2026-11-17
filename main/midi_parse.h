// MIDI 1.0 byte-stream parser shared by the DIN (UART) input and the USB input.
// Portable C, unit-tested on the host.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIDI_SYSEX_MAX 1024

// Called with one complete message: a channel message (2-3 bytes) or a SysEx (F0 ... F7).
typedef void (*midi_msg_fn)(void *ctx, int64_t t_us, const uint8_t *msg, size_t len);

typedef struct {
    midi_msg_fn cb;
    void *ctx;
    uint8_t status;         // running status, 0 = none
    uint8_t d1;
    uint8_t have;           // data bytes collected for the current message
    bool in_sysex;
    bool sysex_overflow;
    uint16_t sx_len;
    uint8_t sx[MIDI_SYSEX_MAX];
} midi_parser_t;

void midi_parser_init(midi_parser_t *p, midi_msg_fn cb, void *ctx);
void midi_parser_feed(midi_parser_t *p, int64_t t_us, const uint8_t *bytes, size_t n);

// USB-MIDI 1.0 event packets (4 bytes each) -> the same parser.
void midi_parser_feed_usb(midi_parser_t *p, int64_t t_us, const uint8_t *pkts, size_t n);
