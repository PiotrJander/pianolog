// Standard MIDI File (format 0) encoding and a streaming scanner used for crash repair.
// Portable C: no ESP-IDF dependencies, unit-tested on the host (see test/).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMF_HEADER_LEN  22      // MThd chunk (14) + "MTrk" + 4-byte length
#define SMF_TRKLEN_OFF  18      // file offset of the big-endian MTrk length
#define SMF_PPQ         1000    // with the 60 BPM tempo below, 1 tick == 1 ms
#define SMF_EVENT_MAX   8       // max encoded size of a channel event (4 delta + 3 msg)

size_t smf_write_header(uint8_t *out);                 // 22 bytes, track length left at 0
size_t smf_write_varlen(uint8_t *out, uint32_t v);     // 1..4 bytes
size_t smf_encode_tempo_60bpm(uint8_t *out);           // delta 0 + FF 51 03 0F 42 40
size_t smf_encode_channel(uint8_t *out, uint32_t delta, const uint8_t *msg, size_t len);
// msg = F0 ... F7 (complete SysEx). Output needs len + 8 bytes.
size_t smf_encode_sysex(uint8_t *out, uint32_t delta, const uint8_t *msg, size_t len);
size_t smf_encode_text(uint8_t *out, uint32_t delta, uint8_t type, const char *text);
size_t smf_encode_eot(uint8_t *out, uint32_t delta);   // delta + FF 2F 00
void   smf_put_be32(uint8_t *out, uint32_t v);

typedef int (*smf_read_fn)(void *ctx);                 // next byte 0..255, or -1 at EOF

typedef struct {
    bool     header_ok;   // looks like one of our files
    bool     has_eot;     // an End-of-Track meta event was found
    uint32_t trk_len;     // length stored in the MTrk header
    uint32_t valid_end;   // file offset just past the last complete event
    uint32_t ticks;       // sum of deltas of complete events (== milliseconds)
    uint32_t notes;       // number of Note On events with velocity > 0
} smf_scan_t;

void smf_scan(smf_read_fn rd, void *ctx, smf_scan_t *out);
