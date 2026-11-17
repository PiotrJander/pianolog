#include "smf.h"
#include <string.h>

void smf_put_be32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);  out[3] = (uint8_t)v;
}

size_t smf_write_header(uint8_t *out)
{
    static const uint8_t hdr[SMF_HEADER_LEN] = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6,
        0, 0,                               // format 0
        0, 1,                               // one track
        (SMF_PPQ >> 8) & 0x7F, SMF_PPQ & 0xFF,
        'M', 'T', 'r', 'k', 0, 0, 0, 0      // length patched when the take is closed
    };
    memcpy(out, hdr, sizeof hdr);
    return sizeof hdr;
}

size_t smf_write_varlen(uint8_t *out, uint32_t v)
{
    if (v > 0x0FFFFFFF) v = 0x0FFFFFFF;
    uint8_t tmp[4];
    int n = 0;
    do { tmp[n++] = v & 0x7F; v >>= 7; } while (v);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i] | (i < n - 1 ? 0x80 : 0);
    return (size_t)n;
}

size_t smf_encode_tempo_60bpm(uint8_t *out)
{
    static const uint8_t ev[] = { 0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40 };   // 1 000 000 us per quarter
    memcpy(out, ev, sizeof ev);
    return sizeof ev;
}

size_t smf_encode_channel(uint8_t *out, uint32_t delta, const uint8_t *msg, size_t len)
{
    size_t n = smf_write_varlen(out, delta);
    memcpy(out + n, msg, len);          // always a full status byte: no running status
    return n + len;
}

size_t smf_encode_sysex(uint8_t *out, uint32_t delta, const uint8_t *msg, size_t len)
{
    if (len < 2 || msg[0] != 0xF0) return 0;
    size_t n = smf_write_varlen(out, delta);
    out[n++] = 0xF0;
    n += smf_write_varlen(out + n, (uint32_t)(len - 1));    // bytes after F0, including F7
    memcpy(out + n, msg + 1, len - 1);
    return n + len - 1;
}

size_t smf_encode_text(uint8_t *out, uint32_t delta, uint8_t type, const char *text)
{
    size_t tl = strlen(text);
    if (tl > 120) tl = 120;
    size_t n = smf_write_varlen(out, delta);
    out[n++] = 0xFF; out[n++] = type; out[n++] = (uint8_t)tl;
    memcpy(out + n, text, tl);
    return n + tl;
}

size_t smf_encode_eot(uint8_t *out, uint32_t delta)
{
    size_t n = smf_write_varlen(out, delta);
    out[n++] = 0xFF; out[n++] = 0x2F; out[n++] = 0x00;
    return n;
}

// ---- streaming scanner ------------------------------------------------------------------

typedef struct { smf_read_fn rd; void *ctx; uint32_t off; bool eof; } rdr_t;

static int rb(rdr_t *r)
{
    if (r->eof) return -1;
    int c = r->rd(r->ctx);
    if (c < 0) { r->eof = true; return -1; }
    r->off++;
    return c;
}

static bool rvar(rdr_t *r, uint32_t *v)
{
    uint32_t x = 0;
    for (int i = 0; i < 4; i++) {
        int c = rb(r);
        if (c < 0) return false;
        x = (x << 7) | (uint32_t)(c & 0x7F);
        if (!(c & 0x80)) { *v = x; return true; }
    }
    return false;       // more than 4 bytes: corrupt
}

static bool rskip(rdr_t *r, uint32_t n)
{
    while (n--) if (rb(r) < 0) return false;
    return true;
}

void smf_scan(smf_read_fn rd, void *ctx, smf_scan_t *out)
{
    memset(out, 0, sizeof *out);
    rdr_t r = { rd, ctx, 0, false };
    uint8_t h[SMF_HEADER_LEN];
    for (int i = 0; i < SMF_HEADER_LEN; i++) {
        int c = rb(&r);
        if (c < 0) return;
        h[i] = (uint8_t)c;
    }
    if (memcmp(h, "MThd", 4) != 0 || memcmp(h + 14, "MTrk", 4) != 0) return;
    out->header_ok = true;
    out->trk_len = ((uint32_t)h[18] << 24) | ((uint32_t)h[19] << 16) | ((uint32_t)h[20] << 8) | h[21];
    out->valid_end = SMF_HEADER_LEN;

    uint8_t running = 0;
    for (;;) {
        uint32_t delta, len;
        if (!rvar(&r, &delta)) return;
        int s = rb(&r);
        if (s < 0) return;
        bool note_on = false;
        if (s == 0xFF) {
            int type = rb(&r);
            if (type < 0 || !rvar(&r, &len) || !rskip(&r, len)) return;
            if (type == 0x2F) {
                out->ticks += delta;
                out->valid_end = r.off;
                out->has_eot = true;
                return;
            }
        } else if (s == 0xF0 || s == 0xF7) {
            if (!rvar(&r, &len) || !rskip(&r, len)) return;
            running = 0;
        } else {
            int d1;
            if (s & 0x80) { running = (uint8_t)s; d1 = rb(&r); if (d1 < 0) return; }
            else { if (!running) return; d1 = s; }
            uint8_t hi = running & 0xF0;
            if (hi < 0x80 || hi == 0xF0) return;                 // not something we ever write
            if (hi != 0xC0 && hi != 0xD0) {
                int d2 = rb(&r);
                if (d2 < 0) return;
                note_on = (hi == 0x90 && d2 > 0);
            }
        }
        out->ticks += delta;
        if (note_on) out->notes++;
        out->valid_end = r.off;
    }
}
