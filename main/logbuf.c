// Log lines over Wi-Fi, in the spirit of `esphome logs`.
//
//   tools/logs.sh                 # follow http://pianolog.local/api/log
//   curl http://pianolog.local/api/log
//
// Every ESP_LOGx line still goes to the USB serial port and is also copied into a 16 kB ring
// buffer. GET /api/log?since=N returns the text written after byte N and puts the position to
// ask from next in the X-Log-Next header; a client polls with that. Nothing is pushed, so
// sending logs never produces log lines of its own and a slow client can't stall a task.
//
// The ring sits in RAM that a software reset doesn't clear, so after a crash, watchdog or
// OTA restart the lines that led up to it are still there, followed by a reset marker.
// Power-on starts empty. Panic backtraces are printed by the ROM, not through esp_log, so
// they only appear on the serial port.
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "app.h"

#define LOG_RING   (16 * 1024)              // power of two
#define LOG_MAGIC  0x504c4f47u              // "PLOG"
#define LOG_LINE   200

typedef struct {
    uint32_t magic;
    uint32_t head;                          // total bytes ever written; position = head % LOG_RING
    uint32_t check;                         // ~magic ^ head, catches a torn or random header
    char data[LOG_RING];
} ring_t;

static __NOINIT_ATTR ring_t s_ring;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_uart;               // the original sink (USB serial / UART)

// The P4 reaches internal RAM through a write-back cache: without this, the last lines before
// a reset are still in the cache and come back as garbage after it.
static void write_back(void *p, size_t n)
{
    esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static void ring_put(const char *p, size_t n)
{
    taskENTER_CRITICAL(&s_mux);
    uint32_t start = s_ring.head & (LOG_RING - 1);
    for (size_t i = 0; i < n; i++) s_ring.data[(s_ring.head + i) & (LOG_RING - 1)] = p[i];
    s_ring.head += n;
    s_ring.check = ~LOG_MAGIC ^ s_ring.head;
    taskEXIT_CRITICAL(&s_mux);

    size_t first = n < LOG_RING - start ? n : LOG_RING - start;
    write_back(&s_ring.data[start], first);
    if (first < n) write_back(&s_ring.data[0], n - first);
    write_back(&s_ring, offsetof(ring_t, data));
}

static int log_vprintf(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int r = s_uart ? s_uart(fmt, ap) : vprintf(fmt, ap);
    char line[LOG_LINE];
    int n = vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    if (n > 0) ring_put(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
    return r;
}

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_SW:       return "software restart";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    default:               return "reset";
    }
}

void logbuf_start(void)
{
    if (s_ring.magic != LOG_MAGIC || s_ring.check != (~LOG_MAGIC ^ s_ring.head)) {
        s_ring.magic = LOG_MAGIC;           // power-on: RAM content is random
        s_ring.head = 0;
        s_ring.check = ~LOG_MAGIC;
    } else {
        char m[64];
        int n = snprintf(m, sizeof m, "\n----- %s -----\n", reset_name(esp_reset_reason()));
        ring_put(m, (size_t)n);
    }
    s_uart = esp_log_set_vprintf(log_vprintf);
}

// GET /api/log[?since=N]  ->  text/plain, X-Log-Next: <position to pass as since next time>
int log_get_handler(httpd_req_t *req)
{
    uint32_t since = 0;
    char q[48], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK &&
        httpd_query_key_value(q, "since", v, sizeof v) == ESP_OK)
        since = (uint32_t)strtoul(v, NULL, 10);

    taskENTER_CRITICAL(&s_mux);
    uint32_t head = s_ring.head;
    taskEXIT_CRITICAL(&s_mux);
    uint32_t oldest = head > LOG_RING ? head - LOG_RING : 0;
    if (since > head) since = oldest;       // the counter went backwards: power-cycled since
    if (since < oldest) since = oldest;     // fell behind: start from what is still there

    char next[16];
    snprintf(next, sizeof next, "%u", (unsigned)head);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Log-Next", next);

    // Copy out in pieces: new lines keep arriving while we send, so take a snapshot per chunk
    // and drop it if the writer lapped us meanwhile.
    char *buf = malloc(1024);
    if (!buf) return httpd_resp_send(req, NULL, 0);
    for (uint32_t pos = since; pos < head;) {
        size_t n = head - pos < 1024 ? head - pos : 1024;
        taskENTER_CRITICAL(&s_mux);
        bool lapped = s_ring.head - pos > LOG_RING;
        if (!lapped)
            for (size_t i = 0; i < n; i++) buf[i] = s_ring.data[(pos + i) & (LOG_RING - 1)];
        taskEXIT_CRITICAL(&s_mux);
        if (lapped) break;
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
        pos += n;
    }
    free(buf);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// A bare page that follows the log, for a phone.
int log_page_handler(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
        "<title>pianolog · log</title><style>body{margin:0;background:#fcfaf5;color:#2a2722}"
        "pre{margin:0;padding:14px 16px;font:12px/1.5 ui-monospace,Menlo,monospace;white-space:pre-wrap}</style>"
        "<pre id=l></pre><script>let n=0,l=document.getElementById('l');"
        "async function t(){try{let r=await fetch('/api/log?since='+n,{cache:'no-store'});"
        "let x=+r.headers.get('X-Log-Next');if(x<n)l.textContent='';n=x;let s=await r.text();"
        "if(s){let b=innerHeight+scrollY>=document.body.scrollHeight-40;l.textContent+=s;"
        "if(l.textContent.length>200000)l.textContent=l.textContent.slice(-150000);"
        "if(b)scrollTo(0,document.body.scrollHeight)}}catch(e){}setTimeout(t,1000)}t()</script>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, sizeof page - 1);
}
