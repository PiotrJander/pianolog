// Settings live in a plain text file on the SD card so they can be edited on any computer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "app.h"

static const char *TAG = "cfg";

pl_config_t g_cfg = {
    .wifi_ssid = "",
    .wifi_pass = "",
    .ap_pass = "",
    .hostname = "pianolog",
    .tz = "CET-1CEST,M3.5.0,M10.5.0/3",     // Europe/Warsaw
    .gap_s = 60,
};

static const char TEMPLATE[] =
    "# pianolog settings. Edit, save, put the card back and power-cycle the recorder.\n"
    "\n"
    "# A home Wi-Fi (2.4 GHz). Tried after the network saved from the setup page\n"
    "# (http://192.168.4.1/setup) and before the one built into the firmware.\n"
    "wifi_ssid=\n"
    "wifi_pass=\n"
    "\n"
    "# Password for the recorder's own setup network (8+ characters).\n"
    "# Empty = the password built into the firmware.\n"
    "ap_pass=\n"
    "\n"
    "# The web page is served at http://<hostname>.local\n"
    "hostname=pianolog\n"
    "\n"
    "# Time zone as a POSIX TZ string (this one is Europe/Warsaw).\n"
    "tz=CET-1CEST,M3.5.0,M10.5.0/3\n"
    "\n"
    "# Seconds of silence that end a take and start a new file.\n"
    "gap_s=60\n";

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void set_str(char *dst, size_t cap, const char *v)
{
    strncpy(dst, v, cap - 1);
    dst[cap - 1] = 0;
}

void config_load(void)
{
    FILE *f = fopen(PL_CONFIG_FILE, "r");
    if (!f) {
        f = fopen(PL_CONFIG_FILE, "w");
        if (f) {
            fputs(TEMPLATE, f);
            fclose(f);
            ESP_LOGI(TAG, "wrote template %s", PL_CONFIG_FILE);
        }
        return;
    }
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (*s == '#' || *s == 0) continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (!strcmp(k, "wifi_ssid")) set_str(g_cfg.wifi_ssid, sizeof g_cfg.wifi_ssid, v);
        else if (!strcmp(k, "wifi_pass")) set_str(g_cfg.wifi_pass, sizeof g_cfg.wifi_pass, v);
        else if (!strcmp(k, "ap_pass")) set_str(g_cfg.ap_pass, sizeof g_cfg.ap_pass, v);
        else if (!strcmp(k, "hostname") && *v) set_str(g_cfg.hostname, sizeof g_cfg.hostname, v);
        else if (!strcmp(k, "tz") && *v) set_str(g_cfg.tz, sizeof g_cfg.tz, v);
        else if (!strcmp(k, "gap_s")) {
            int g = atoi(v);
            if (g >= 5 && g <= 3600) g_cfg.gap_s = g;
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "config: ssid='%s' hostname='%s' gap=%ds", g_cfg.wifi_ssid, g_cfg.hostname, g_cfg.gap_s);
}
