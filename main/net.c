// Wi-Fi through the on-board ESP32-C6 (esp_hosted + esp_wifi_remote make the normal
// esp_wifi API work on the radio-less ESP32-P4), SNTP time, and mDNS.
//
// Networks are tried in this order, moving on after two failed attempts:
//   1. the one saved from the setup page (NVS)
//   2. wifi_ssid / wifi_pass from pianolog.txt on the SD card
//   3. CONFIG_PL_WIFI_SSID / CONFIG_PL_WIFI_PASS compiled into the firmware
// When none connects within 30 s, or none is configured, the recorder opens its own
// password-protected network (name = hostname, password = ap_pass) and serves the setup page
// at http://192.168.4.1/setup. It keeps retrying the home networks in the background, more
// slowly so the phone on the setup page is not disturbed, and closes its own network once a
// home network is joined.
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "net";

#define AP_FALLBACK_US      (30 * 1000 * 1000)
#define RETRY_US            (8 * 1000 * 1000)       // between attempts while no AP is up
#define RETRY_WITH_AP_US    (60 * 1000 * 1000)      // between attempts while the setup network is up
#define SCAN_QUIET_US       (120 * 1000 * 1000)     // no attempts after someone scanned for networks
#define ATTEMPTS_PER_NET    2
#define MAX_CANDIDATES      3

typedef struct { char ssid[33]; char pass[65]; const char *src; } cred_t;

static cred_t s_cand[MAX_CANDIDATES];
static int s_ncand, s_cur, s_attempts;
static volatile bool s_connecting, s_reload;
static volatile int64_t s_down_since, s_next_try, s_up_since;
static pl_net_status_t s_ns;
static esp_netif_t *s_sta_netif;
static bool s_wifi_ok, s_sntp_started;

bool time_is_valid(void) { return time(NULL) > 1735689600; }       // after 2025-01-01
void net_get_status(pl_net_status_t *out) { *out = s_ns; }
bool net_is_up(void) { return s_ns.sta_connected || s_ns.ap_active; }

// ---- candidate networks -----------------------------------------------------------------

static void add_cand(const char *ssid, const char *pass, const char *src)
{
    if (!ssid || !ssid[0] || s_ncand >= MAX_CANDIDATES) return;
    for (int i = 0; i < s_ncand; i++)
        if (!strcmp(s_cand[i].ssid, ssid) && !strcmp(s_cand[i].pass, pass ? pass : "")) return;
    strlcpy(s_cand[s_ncand].ssid, ssid, sizeof s_cand[0].ssid);
    strlcpy(s_cand[s_ncand].pass, pass ? pass : "", sizeof s_cand[0].pass);
    s_cand[s_ncand].src = src;
    s_ncand++;
}

static void load_candidates(void)
{
    s_ncand = 0;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        char ssid[33] = "", pass[65] = "";
        size_t l1 = sizeof ssid, l2 = sizeof pass;
        if (nvs_get_str(h, "ssid", ssid, &l1) == ESP_OK) {
            if (nvs_get_str(h, "pass", pass, &l2) != ESP_OK) pass[0] = 0;
            add_cand(ssid, pass, "setup page");
        }
        nvs_close(h);
    }
    add_cand(g_cfg.wifi_ssid, g_cfg.wifi_pass, "pianolog.txt");
    if (strcmp(CONFIG_PL_WIFI_SSID, "YOUR_WIFI_SSID") != 0)
        add_cand(CONFIG_PL_WIFI_SSID, CONFIG_PL_WIFI_PASS, "firmware");
    s_cur = 0;
    s_attempts = 0;
    for (int i = 0; i < s_ncand; i++) ESP_LOGI(TAG, "network %d: '%s' (from %s)", i + 1, s_cand[i].ssid, s_cand[i].src);
}

static void try_connect(void)
{
    if (s_attempts >= ATTEMPTS_PER_NET) {
        s_cur = (s_cur + 1) % s_ncand;
        s_attempts = 0;
    }
    const cred_t *c = &s_cand[s_cur];
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, c->ssid, sizeof sta.sta.ssid);
    strlcpy((char *)sta.sta.password, c->pass, sizeof sta.sta.password);
    sta.sta.threshold.authmode = c->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) return;
    s_attempts++;
    s_connecting = true;
    strlcpy(s_ns.ssid, c->ssid, sizeof s_ns.ssid);
    ESP_LOGI(TAG, "joining '%s' (attempt %d)", c->ssid, s_attempts);
    if (esp_wifi_connect() != ESP_OK) s_connecting = false;
}

// ---- access point -----------------------------------------------------------------------

static void set_ap(bool on)
{
    if (on == s_ns.ap_active) return;
    if (!on) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        s_ns.ap_active = false;
        ESP_LOGI(TAG, "home network is up, setup network closed");
        return;
    }
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, g_cfg.hostname, sizeof ap.ap.ssid);
    ap.ap.ssid_len = (uint8_t)strlen(g_cfg.hostname);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    strlcpy((char *)ap.ap.password, g_cfg.ap_pass, sizeof ap.ap.password);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (strlen(g_cfg.ap_pass) < 8) {        // WPA2 needs 8+ characters: fall back to the built-in one
        strlcpy((char *)ap.ap.password, CONFIG_PL_AP_PASS, sizeof ap.ap.password);
        ESP_LOGW(TAG, "ap_pass shorter than 8 characters, using the built-in password");
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);    // STA stays on for retries and scans
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) { ESP_LOGE(TAG, "setup network: %s", esp_err_to_name(err)); return; }
    s_ns.ap_active = true;
    if (!s_ns.sta_connected) strcpy(s_ns.ip, "192.168.4.1");
    ESP_LOGI(TAG, "setup network '%s' up: http://192.168.4.1/setup", g_cfg.hostname);
}

// ---- events and supervision -------------------------------------------------------------

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_ns.sta_connected) {
            s_down_since = esp_timer_get_time();
            s_attempts = 0;             // a known-good network dropped: retry it first
        }
        s_ns.sta_connected = false;
        s_connecting = false;
        s_up_since = 0;
        if (s_ns.ap_active) strcpy(s_ns.ip, "192.168.4.1");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_ns.ip, sizeof s_ns.ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_ns.sta_connected = true;
        s_connecting = false;
        s_attempts = 0;
        s_up_since = esp_timer_get_time();
        ESP_LOGI(TAG, "joined '%s': http://%s.local or http://%s", s_ns.ssid, g_cfg.hostname, s_ns.ip);
        if (!s_sntp_started) {
            esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            if (esp_netif_sntp_init(&sc) == ESP_OK) s_sntp_started = true;
        }
    }
}

static void supervisor_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        if (s_reload) {                 // new credentials from the setup page
            s_reload = false;
            load_candidates();
            s_next_try = 0;
            s_connecting = false;
        }
        if (s_ns.sta_connected) {
            // Keep the setup network a little longer so the phone sees the success message.
            if (s_ns.ap_active && s_up_since && now - s_up_since > 15 * 1000000LL) set_ap(false);
            continue;
        }
        if (!s_ns.ap_active && (s_ncand == 0 || now - s_down_since > AP_FALLBACK_US)) set_ap(true);
        if (s_ncand == 0 || s_connecting || now < s_next_try) continue;
        try_connect();
        s_next_try = now + (s_ns.ap_active ? RETRY_WITH_AP_US : RETRY_US);
    }
}

// ---- public -----------------------------------------------------------------------------

esp_err_t net_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved network '%s' from the setup page", ssid);
        s_reload = true;
    }
    return err;
}

static void json_str(char **p, char *end, const char *s)
{
    if (*p < end) *(*p)++ = '"';
    for (; *s && *p < end - 2; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { *(*p)++ = '\\'; *(*p)++ = (char)c; }
        else if (c >= 0x20) *(*p)++ = (char)c;
    }
    if (*p < end) *(*p)++ = '"';
}

esp_err_t net_scan_json(char *out, size_t cap)
{
    if (!s_wifi_ok) return ESP_ERR_INVALID_STATE;
    s_next_try = esp_timer_get_time() + SCAN_QUIET_US;     // keep the radio on this channel
    if (s_connecting) { esp_wifi_disconnect(); s_connecting = false; vTaskDelay(pdMS_TO_TICKS(300)); }
    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        err = esp_wifi_scan_start(NULL, true);
        if (err != ESP_OK) return err;
    }
    uint16_t n = 20;
    wifi_ap_record_t *recs = calloc(n, sizeof *recs);
    if (!recs) { esp_wifi_clear_ap_list(); return ESP_ERR_NO_MEM; }
    esp_wifi_scan_get_ap_records(&n, recs);
    char *p = out, *end = out + cap - 2;
    *p++ = '[';
    int written = 0;
    for (int i = 0; i < n && p < end - 120; i++) {
        if (!recs[i].ssid[0]) continue;
        bool dup = false;                   // one entry per name (mesh networks repeat it)
        for (int j = 0; j < i && !dup; j++) dup = !strcmp((char *)recs[j].ssid, (char *)recs[i].ssid);
        if (dup) continue;
        if (written++) *p++ = ',';
        p += snprintf(p, end - p, "{\"ssid\":");
        json_str(&p, end, (const char *)recs[i].ssid);
        p += snprintf(p, end - p, ",\"rssi\":%d,\"open\":%s}", recs[i].rssi,
                      recs[i].authmode == WIFI_AUTH_OPEN ? "true" : "false");
    }
    *p++ = ']';
    *p = 0;
    free(recs);
    return ESP_OK;
}

void net_start(void)
{
    if (strlen(g_cfg.ap_pass) < 8) strlcpy(g_cfg.ap_pass, CONFIG_PL_AP_PASS, sizeof g_cfg.ap_pass);
    load_candidates();

    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta_netif, g_cfg.hostname);

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi unavailable (%s). Recording still works; read the card in a computer.",
                 esp_err_to_name(err));
        return;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);     // credentials are managed here, not by the driver
    esp_wifi_set_mode(WIFI_MODE_STA);
    err = esp_wifi_start();
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err)); return; }
    s_wifi_ok = true;
    s_down_since = esp_timer_get_time();
    if (s_ncand == 0) set_ap(true);

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(g_cfg.hostname);
        mdns_instance_name_set("pianolog MIDI recorder");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    xTaskCreate(supervisor_task, "net_sup", 4096, NULL, 3, NULL);
}
