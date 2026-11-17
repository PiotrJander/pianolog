// Host-side unit tests for the portable parts: gcc -I../main host_test.c ../main/smf.c ../main/midi_parse.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "midi_parse.h"
#include "smf.h"

static uint8_t file[65536];
static size_t flen;
static uint32_t last_ms;
static int msgs, sysexes;

static void emit(const uint8_t *p, size_t n) { memcpy(file + flen, p, n); flen += n; }

static void on_msg(void *ctx, int64_t t_us, const uint8_t *msg, size_t len)
{
    uint8_t tmp[1100];
    uint32_t ms = (uint32_t)(t_us / 1000);
    size_t n = len <= 3 ? smf_encode_channel(tmp, ms - last_ms, msg, len)
                        : smf_encode_sysex(tmp, ms - last_ms, msg, len);
    emit(tmp, n);
    last_ms = ms;
    msgs++;
    if (len > 3) sysexes++;
}

typedef struct { const uint8_t *p; size_t n, i; } mem_t;
static int mem_rd(void *c) { mem_t *m = c; return m->i < m->n ? m->p[m->i++] : -1; }

int main(void)
{
    uint8_t tmp[64];
    // varlen encodings from the SMF spec
    assert(smf_write_varlen(tmp, 0) == 1 && tmp[0] == 0x00);
    assert(smf_write_varlen(tmp, 0x7F) == 1 && tmp[0] == 0x7F);
    assert(smf_write_varlen(tmp, 0x80) == 2 && tmp[0] == 0x81 && tmp[1] == 0x00);
    assert(smf_write_varlen(tmp, 0x3FFF) == 2 && tmp[0] == 0xFF && tmp[1] == 0x7F);
    assert(smf_write_varlen(tmp, 0x4000) == 3 && tmp[0] == 0x81 && tmp[1] == 0x80 && tmp[2] == 0x00);
    assert(smf_write_varlen(tmp, 0x0FFFFFFF) == 4 && tmp[0] == 0xFF && tmp[3] == 0x7F);

    flen = smf_write_header(file);
    flen += smf_encode_tempo_60bpm(file + flen);
    flen += smf_encode_text(file + flen, 0, 0x03, "pianolog test");

    midi_parser_t p;
    midi_parser_init(&p, on_msg, NULL);

    // DIN-style stream: running status, interleaved real-time bytes, a SysEx, system common.
    const uint8_t s1[] = { 0x90, 60, 100, 0xFE, 64, 0xF8, 90, 67, 80 };         // 3 note-ons via running status
    midi_parser_feed(&p, 0, s1, sizeof s1);
    const uint8_t s2[] = { 0xB0, 64, 127 };                                     // pedal down
    midi_parser_feed(&p, 250000, s2, sizeof s2);
    const uint8_t s3[] = { 0xF0, 0x43, 0x10, 0xFE, 0x4C, 0x00, 0xF7 };          // SysEx with active sensing inside
    midi_parser_feed(&p, 400000, s3, sizeof s3);
    const uint8_t s4[] = { 0xF2, 0x01, 0x02, 0xC0, 5, 0x80, 60, 0, 64, 0, 67, 0 };   // song pos (dropped), PC, 3 note-offs
    midi_parser_feed(&p, 1500000, s4, sizeof s4);
    assert(msgs == 3 + 1 + 1 + 1 + 3);
    assert(sysexes == 1);

    // USB-MIDI packets: note on, note off, 2-byte program change, real-time (ignored), 7-byte SysEx in 3 packets
    const uint8_t u[] = {
        0x09, 0x90, 72, 110,   0x08, 0x80, 72, 0,   0x0C, 0xC1, 9, 0,   0x0F, 0xFE, 0, 0,
        0x04, 0xF0, 0x7E, 0x7F,   0x04, 0x09, 0x01, 0x02,   0x05, 0xF7, 0, 0,
    };
    int before = msgs;
    midi_parser_feed_usb(&p, 200000000, u, sizeof u);       // 200 s later: 3-byte delta
    assert(msgs - before == 4 && sysexes == 2);

    size_t complete = flen;                                 // file as it is on the card mid-take
    flen += smf_encode_eot(file + flen, 0);
    smf_put_be32(file + SMF_TRKLEN_OFF, (uint32_t)(flen - SMF_HEADER_LEN));

    smf_scan_t sc;
    mem_t m = { file, flen, 0 };
    smf_scan(mem_rd, &m, &sc);
    assert(sc.header_ok && sc.has_eot);
    assert(sc.valid_end == flen && sc.trk_len == flen - SMF_HEADER_LEN);
    assert(sc.notes == 4);
    assert(sc.ticks == 200000);

    // Power-loss simulation: every possible truncation must scan to an event boundary.
    uint32_t prev = SMF_HEADER_LEN;
    for (size_t cut = SMF_HEADER_LEN; cut <= complete; cut++) {
        mem_t t = { file, cut, 0 };
        smf_scan(mem_rd, &t, &sc);
        assert(sc.header_ok && !sc.has_eot);
        assert(sc.valid_end <= cut && sc.valid_end >= prev);
        prev = sc.valid_end;
    }
    assert(prev == complete);

    FILE *f = fopen("out.mid", "wb");
    fwrite(file, 1, flen, f);
    fclose(f);
    // The same take as the web server would send it while still recording.
    f = fopen("live.mid", "wb");
    uint8_t hdr[SMF_HEADER_LEN];
    memcpy(hdr, file, SMF_HEADER_LEN);
    smf_put_be32(hdr + SMF_TRKLEN_OFF, (uint32_t)(complete - SMF_HEADER_LEN + 4));
    fwrite(hdr, 1, SMF_HEADER_LEN, f);
    fwrite(file + SMF_HEADER_LEN, 1, complete - SMF_HEADER_LEN, f);
    size_t el = smf_encode_eot(tmp, 0);
    fwrite(tmp, 1, el, f);
    fclose(f);
    printf("host tests passed, %zu bytes\n", flen);
    return 0;
}
