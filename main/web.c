// Tiny HTTP server: one embedded page plus a JSON API over the takes on the SD card.
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "app.h"
#include "smf.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

// Names come from URLs: allow only what the recorder itself generates.
static bool valid_name(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= 40 || s[0] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return strstr(s, "..") == NULL;
}

static bool is_mid(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && strcmp(name + l - 4, ".mid") == 0;
}

static bool query_get(httpd_req_t *req, const char *key, char *out, size_t cap)
{
    char q[160];
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, cap) == ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, msg);
}

// ---- GET / ------------------------------------------------------------------------------

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

// ---- GET /fonts/<name>.woff2 -------------------------------------------------------------
// Cormorant Garamond and EB Garamond (SIL Open Font License), latin subset, kept on the
// device so the page looks the same on the recorder's own network, which has no internet.

extern const uint8_t cormorant_start[] asm("_binary_cormorant_woff2_start");
extern const uint8_t cormorant_end[] asm("_binary_cormorant_woff2_end");
extern const uint8_t cormorant_italic_start[] asm("_binary_cormorant_italic_woff2_start");
extern const uint8_t cormorant_italic_end[] asm("_binary_cormorant_italic_woff2_end");
extern const uint8_t garamond_start[] asm("_binary_garamond_woff2_start");
extern const uint8_t garamond_end[] asm("_binary_garamond_woff2_end");
extern const uint8_t garamond_italic_start[] asm("_binary_garamond_italic_woff2_start");
extern const uint8_t garamond_italic_end[] asm("_binary_garamond_italic_woff2_end");

static esp_err_t font_get(httpd_req_t *req)
{
    static const struct { const char *name; const uint8_t *start, *end; } fonts[] = {
        { "cormorant.woff2",        cormorant_start,        cormorant_end },
        { "cormorant-italic.woff2", cormorant_italic_start, cormorant_italic_end },
        { "garamond.woff2",         garamond_start,         garamond_end },
        { "garamond-italic.woff2",  garamond_italic_start,  garamond_italic_end },
    };
    const char *name = req->uri + strlen("/fonts/");
    for (size_t i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        if (strcmp(name, fonts[i].name) == 0) {
            httpd_resp_set_type(req, "font/woff2");
            httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
            return httpd_resp_send(req, (const char *)fonts[i].start, fonts[i].end - fonts[i].start);
        }
    }
    return httpd_resp_send_404(req);
}

// ---- GET /api/status --------------------------------------------------------------------

static esp_err_t status_get(httpd_req_t *req)
{
    pl_rec_status_t rs;
    pl_net_status_t ns;
    recorder_get_status(&rs);
    net_get_status(&ns);
    char usb_name[48];
    bool usb = midi_usb_connected(usb_name, sizeof usb_name);
    uint64_t total = 0, free_b = 0, ftotal = 0, ffree = 0;
    bool sd = storage_info(&total, &free_b);
    bool fl = flash_info(&ftotal, &ffree);

    char buf[900];
    snprintf(buf, sizeof buf,
             "{\"version\":\"" PL_VERSION "\",\"time\":%lld,\"time_valid\":%s,"
             "\"sd\":%s,\"sd_free_mb\":%u,\"sd_total_mb\":%u,\"flash\":%s,\"flash_free_kb\":%u,\"flash_total_kb\":%u,"
             "\"usb\":%s,\"usb_name\":\"%s\",\"events_usb\":%u,\"events_din\":%u,"
             "\"recording\":%s,\"rec_day\":\"%s\",\"rec_file\":\"%s\",\"rec_s\":%u,\"rec_notes\":%u,"
             "\"takes\":%u,\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gap_s\":%d}",
             (long long)time(NULL), time_is_valid() ? "true" : "false",
             sd ? "true" : "false", (unsigned)(free_b >> 20), (unsigned)(total >> 20),
             fl ? "true" : "false", (unsigned)(ffree >> 10), (unsigned)(ftotal >> 10),
             usb ? "true" : "false", usb_name, (unsigned)rs.events_usb, (unsigned)rs.events_din,
             rs.recording ? "true" : "false", rs.day, rs.file, (unsigned)rs.elapsed_s, (unsigned)rs.notes,
             (unsigned)rs.takes_since_boot, ns.sta_connected ? "sta" : (ns.ap_active ? "ap" : "none"),
             ns.sta_connected ? "home" : "own", ns.ip, g_cfg.gap_s);
    return send_json(req, buf);
}

// ---- GET /api/days ----------------------------------------------------------------------

static int cmp_desc(const void *a, const void *b) { return strcmp((const char *)b, (const char *)a); }

static esp_err_t days_get(httpd_req_t *req)
{
    enum { W = 16 };
    size_t cap = 64, n = 0;
    char (*names)[W] = malloc(cap * W);
    if (!names) return fail(req, "500 Internal Server Error", "out of memory");
    const char *roots[2] = { PL_REC_DIR, storage_ready() ? PL_CARD_REC : NULL };
    for (int r = 0; r < 2; r++) {
        if (!roots[r]) continue;
        DIR *d = opendir(roots[r]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_type != DT_DIR || !valid_name(de->d_name) || strlen(de->d_name) >= W) continue;
            bool dup = false;
            for (size_t i = 0; i < n && !dup; i++) dup = strcmp(names[i], de->d_name) == 0;
            if (dup) continue;
            if (n == cap) {
                void *p = realloc(names, cap * 2 * W);
                if (!p) break;
                names = p;
                cap *= 2;
            }
            strcpy(names[n++], de->d_name);
        }
        closedir(d);
    }
    qsort(names, n, W, cmp_desc);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    char item[W + 8];
    for (size_t i = 0; i < n; i++) {
        snprintf(item, sizeof item, "%s\"%s\"", i ? "," : "", names[i]);
        httpd_resp_sendstr_chunk(req, item);
    }
    free(names);
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

// ---- GET /api/list?day=YYYY-MM-DD -------------------------------------------------------

static esp_err_t list_get(httpd_req_t *req)
{
    char day[20];
    if (!query_get(req, "day", day, sizeof day) || !valid_name(day))
        return fail(req, "400 Bad Request", "bad day");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    const char *roots[2] = { PL_REC_DIR, storage_ready() ? PL_CARD_REC : NULL };
    for (int r = 0; r < 2; r++) {
        if (!roots[r]) continue;
        char dir[PL_PATH_MAX];
        snprintf(dir, sizeof dir, "%s/%s", roots[r], day);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!is_mid(de->d_name) || !valid_name(de->d_name)) continue;
            char path[PL_PATH_MAX + 48];
            struct stat st;
            if (r == 1) {           // skip if the flash copy is still there (move in progress)
                snprintf(path, sizeof path, PL_REC_DIR "/%s/%s", day, de->d_name);
                if (stat(path, &st) == 0) continue;
            }
            snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
            if (stat(path, &st) != 0) continue;
            int dur, notes;
            bool star;
            take_name_parse(de->d_name, &dur, &notes, &star);
            char item[220];
            snprintf(item, sizeof item,
                     "%s{\"f\":\"%s\",\"dur\":%d,\"notes\":%d,\"star\":%s,\"size\":%ld,\"live\":%s,\"where\":\"%s\"}",
                     first ? "" : ",", de->d_name, dur, notes, star ? "true" : "false", (long)st.st_size,
                     (r == 0 && recorder_is_live(day, de->d_name)) ? "true" : "false", r ? "card" : "flash");
            httpd_resp_sendstr_chunk(req, item);
            first = false;
        }
        closedir(d);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

// ---- GET /rec/<day>/<file>.mid ----------------------------------------------------------

static bool split_rec_uri(const char *uri, char *day, size_t dcap, char *file, size_t fcap)
{
    const char *p = uri + 5;                    // past "/rec/"
    const char *q = strchr(p, '?');
    size_t len = q ? (size_t)(q - p) : strlen(p);
    const char *slash = memchr(p, '/', len);
    if (!slash) return false;
    size_t dl = (size_t)(slash - p), fl = len - dl - 1;
    if (dl == 0 || dl >= dcap || fl == 0 || fl >= fcap) return false;
    memcpy(day, p, dl);
    day[dl] = 0;
    memcpy(file, slash + 1, fl);
    file[fl] = 0;
    return valid_name(day) && valid_name(file) && is_mid(file);
}

static esp_err_t file_get(httpd_req_t *req)
{
    char day[20], file[48];
    if (!split_rec_uri(req->uri, day, sizeof day, file, sizeof file))
        return fail(req, "400 Bad Request", "bad path");
    char path[PL_PATH_MAX + 48];
    files_lock();
    FILE *f = take_resolve(day, file, path, sizeof path, NULL) ? fopen(path, "rb") : NULL;
    files_unlock();
    if (!f) return fail(req, "404 Not Found", "no such take");

    char *buf = malloc(2048);
    if (!buf) { fclose(f); return fail(req, "500 Internal Server Error", "out of memory"); }
    char disp[120];
    snprintf(disp, sizeof disp, "attachment; filename=\"piano_%s_%s\"", day, file);
    httpd_resp_set_type(req, "audio/midi");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // A take that is still being recorded (or was cut by a power loss) has no track length
    // and no End-of-Track yet. Complete it in the copy we send; the file itself is untouched.
    bool unfinished = false;
    uint32_t total = 0;
    esp_err_t err = ESP_OK;
    size_t n = fread(buf, 1, 2048, f);
    if (n >= SMF_HEADER_LEN && memcmp(buf + SMF_TRKLEN_OFF, "\0\0\0\0", 4) == 0) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size >= SMF_HEADER_LEN) {
            unfinished = true;
            smf_put_be32((uint8_t *)buf + SMF_TRKLEN_OFF, (uint32_t)st.st_size - SMF_HEADER_LEN + 4);
            total = (uint32_t)st.st_size;
        }
    }
    uint32_t sent = 0;
    while (n > 0 && err == ESP_OK) {
        if (unfinished && sent + n > total) n = total - sent;      // never past the length we promised
        if (n == 0) break;
        err = httpd_resp_send_chunk(req, buf, (ssize_t)n);
        sent += (uint32_t)n;
        n = fread(buf, 1, 2048, f);
    }
    if (err == ESP_OK && unfinished && sent == total) {
        uint8_t eot[4];
        size_t el = smf_encode_eot(eot, 0);
        err = httpd_resp_send_chunk(req, (const char *)eot, (ssize_t)el);
    }
    free(buf);
    fclose(f);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

// ---- POST /api/delete?day=&f=   POST /api/star?day=&f=&on=1 -----------------------------

static bool take_from_query(httpd_req_t *req, char *day, size_t dcap, char *file, size_t fcap)
{
    return query_get(req, "day", day, dcap) && query_get(req, "f", file, fcap) &&
           valid_name(day) && valid_name(file) && is_mid(file);
}

static esp_err_t delete_post(httpd_req_t *req)
{
    char day[20], file[48];
    if (!take_from_query(req, day, sizeof day, file, sizeof file))
        return fail(req, "400 Bad Request", "bad take");
    if (recorder_is_live(day, file)) return fail(req, "409 Conflict", "still recording");
    char path[PL_PATH_MAX + 48];
    files_lock();
    bool found = false;
    while (take_resolve(day, file, path, sizeof path, NULL) && unlink(path) == 0) found = true;  // both copies
    snprintf(path, sizeof path, PL_REC_DIR "/%s", day);
    rmdir(path);                                // succeeds only when the day is now empty
    snprintf(path, sizeof path, PL_CARD_REC "/%s", day);
    if (storage_ready()) rmdir(path);
    files_unlock();
    if (!found) return fail(req, "404 Not Found", "no such take");
    ESP_LOGI(TAG, "deleted %s/%s", day, file);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t star_post(httpd_req_t *req)
{
    char day[20], file[48], on[4] = "1";
    if (!take_from_query(req, day, sizeof day, file, sizeof file))
        return fail(req, "400 Bad Request", "bad take");
    query_get(req, "on", on, sizeof on);
    if (!take_name_parse(file, NULL, NULL, NULL)) return fail(req, "409 Conflict", "take not closed yet");

    char stem[48];
    strlcpy(stem, file, sizeof stem);
    stem[strlen(stem) - 4] = 0;                 // drop ".mid"
    char *s = strstr(stem, "_star");
    if (s) *s = 0;
    char nf[64];
    snprintf(nf, sizeof nf, "%s%s.mid", stem, on[0] == '1' ? "_star" : "");
    if (strcmp(nf, file) != 0) {
        char from[PL_PATH_MAX + 48], to[PL_PATH_MAX + 72];
        files_lock();
        bool ok = take_resolve(day, file, from, sizeof from, NULL);
        if (ok) {
            strlcpy(to, from, sizeof to);
            char *slash = strrchr(to, '/');
            strlcpy(slash + 1, nf, sizeof to - (size_t)(slash + 1 - to));
            ok = rename(from, to) == 0;
        }
        files_unlock();
        if (!ok) return fail(req, "404 Not Found", "no such take");
    }
    char out[96];
    snprintf(out, sizeof out, "{\"f\":\"%s\"}", nf);
    return send_json(req, out);
}

// ---- POST /api/time?t=<unix seconds> ----------------------------------------------------
// The browser lends us its clock when there is no internet (fallback access point).

static esp_err_t time_post(httpd_req_t *req)
{
    char t[24];
    if (!query_get(req, "t", t, sizeof t)) return fail(req, "400 Bad Request", "missing t");
    long long v = atoll(t);
    if (!time_is_valid() && v > 1735689600LL && v < 4102444800LL) {
        struct timeval tv = { .tv_sec = (time_t)v, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock set from browser");
    }
    return send_json(req, "{\"ok\":true}");
}

// ---- Wi-Fi setup ------------------------------------------------------------------------
// Changing the Wi-Fi is allowed only from the recorder's own (password-protected) network,
// so nobody on the home network can move the recorder to another router.

static bool via_own_network(httpd_req_t *req)
{
    struct sockaddr_storage a;
    socklen_t l = sizeof a;
    if (getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&a, &l) != 0) return false;
    if (a.ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&a;
        return (ntohl(in->sin_addr.s_addr) & 0xFFFFFF00u) == 0xC0A80400u;      // 192.168.4.x
    }
    return false;
}

static esp_err_t setup_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start - 1);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    if (!via_own_network(req)) return fail(req, "403 Forbidden", "join the recorder's own network first");
    char *buf = malloc(3072);
    if (!buf) return fail(req, "500 Internal Server Error", "out of memory");
    esp_err_t err = net_scan_json(buf, 3072);
    if (err != ESP_OK) strcpy(buf, "[]");
    send_json(req, buf);
    free(buf);
    return ESP_OK;
}

// Decodes one application/x-www-form-urlencoded value in place.
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    if (!via_own_network(req)) return fail(req, "403 Forbidden", "join the recorder's own network first");
    char body[300];
    if (req->content_len >= sizeof body) return fail(req, "413 Payload Too Large", "too long");
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) return fail(req, "400 Bad Request", "read error");
        got += r;
    }
    body[got] = 0;
    char ssid[33 * 3] = "", pass[65 * 3] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof ssid);
    httpd_query_key_value(body, "pass", pass, sizeof pass);
    url_decode(ssid);
    url_decode(pass);
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64)
        return fail(req, "400 Bad Request", "network name must be 1-32 characters, password at most 64");
    if (net_set_wifi(ssid, pass) != ESP_OK) return fail(req, "500 Internal Server Error", "could not save");
    return send_json(req, "{\"ok\":true}");
}

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 22;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    httpd_handle_t srv;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "http server failed to start");
        return;
    }
    static const httpd_uri_t routes[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = root_get },
        { .uri = "/api/status",  .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/days",    .method = HTTP_GET,  .handler = days_get },
        { .uri = "/api/list",    .method = HTTP_GET,  .handler = list_get },
        { .uri = "/rec/*",       .method = HTTP_GET,  .handler = file_get },
        { .uri = "/api/delete",  .method = HTTP_POST, .handler = delete_post },
        { .uri = "/api/star",    .method = HTTP_POST, .handler = star_post },
        { .uri = "/api/time",    .method = HTTP_POST, .handler = time_post },
        { .uri = "/setup",       .method = HTTP_GET,  .handler = setup_get },
        { .uri = "/api/scan",    .method = HTTP_GET,  .handler = scan_get },
        { .uri = "/api/wifi",    .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/api/update",  .method = HTTP_POST, .handler = ota_post_handler },
        { .uri = "/api/update-wifi", .method = HTTP_POST, .handler = ota_wifi_post_handler },
        { .uri = "/api/log",     .method = HTTP_GET,  .handler = log_get_handler },
        { .uri = "/log",         .method = HTTP_GET,  .handler = log_page_handler },
        { .uri = "/fonts/*",     .method = HTTP_GET,  .handler = font_get },
    };
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) httpd_register_uri_handler(srv, &routes[i]);
}
