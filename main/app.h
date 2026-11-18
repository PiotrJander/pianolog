// pianolog — shared declarations
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PL_VERSION     "0.5.1"
#define PL_MOUNT       "/sdcard"
#define PL_FLASH       "/flash"
#define PL_REC_DIR     PL_FLASH "/rec"      // takes are always recorded here first
#define PL_CARD_REC    PL_MOUNT "/rec"      // closed takes are moved here when a card is present
#define PL_CONFIG_FILE PL_MOUNT "/pianolog.txt"
#define PL_PATH_MAX    96

// ---- config.c -------------------------------------------------------------------------
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char ap_pass[65];       // password of the fallback access point (8+ characters)
    char hostname[32];      // http://<hostname>.local
    char tz[48];            // POSIX TZ string
    int  gap_s;             // silence that closes a take
} pl_config_t;

extern pl_config_t g_cfg;
void config_load(void);     // reads PL_CONFIG_FILE, writes a template if it is missing

// ---- storage.c ------------------------------------------------------------------------
esp_err_t flash_mount(void);                // LittleFS partition "storage" at PL_FLASH
bool flash_ready(void);
bool flash_info(uint64_t *total_bytes, uint64_t *free_bytes);
esp_err_t storage_mount(void);              // microSD card at PL_MOUNT
void storage_unmount(void);
bool storage_ready(void);
bool storage_info(uint64_t *total_bytes, uint64_t *free_bytes);

// Serialises renames/deletes of take files between the web server and the archiver.
void files_lock(void);
void files_unlock(void);

// ---- archive.c ------------------------------------------------------------------------
// Moves closed takes from flash to the card: copy, read back and compare, then delete.
void archive_start(void);
void archive_kick(void);                     // run a pass soon (take closed, card inserted)
// No card and flash nearly full: delete the oldest takes (starred ones last) to make room.
void flash_make_room(void);
// Where a take lives. Returns false if it exists in neither place.
bool take_resolve(const char *day, const char *file, char *path, size_t cap, bool *on_card);

// ---- recorder.c -----------------------------------------------------------------------
typedef enum { SRC_USB = 0, SRC_DIN = 1 } pl_src_t;

typedef struct {
    bool     recording;
    char     day[16];           // directory of the take being recorded
    char     file[40];          // its file name
    uint32_t elapsed_s;
    uint32_t notes;
    uint32_t takes_since_boot;
    uint32_t events_usb;        // messages seen per input since boot
    uint32_t events_din;
} pl_rec_status_t;

void recorder_start(uint32_t boot_count);
// Complete MIDI message from an input. Safe to call from any task.
void recorder_submit(pl_src_t src, int64_t t_us, const uint8_t *msg, size_t len);
void recorder_get_status(pl_rec_status_t *out);
bool recorder_is_live(const char *day, const char *file);
void recorder_repair_dir(const char *root);  // finalise takes cut short by a power loss
// Close the take being recorded (if any) so the device can restart. Waits up to timeout_ms.
bool recorder_finish_for_restart(int timeout_ms);

// Take file names: "<base>.mid" while recording, "<base>_<sec>s_<notes>n[_star].mid" once closed.
bool take_name_parse(const char *name, int *dur_s, int *notes, bool *star);

// ---- midi_usb.c / midi_din.c ----------------------------------------------------------
void midi_usb_start(void);
bool midi_usb_connected(char *name, size_t name_len);
void midi_din_start(void);

// ---- net.c ----------------------------------------------------------------------------
void net_start(void);
typedef struct { bool sta_connected; bool ap_active; char ip[16]; char ssid[33]; } pl_net_status_t;
void net_get_status(pl_net_status_t *out);
bool net_is_up(void);                         // joined a network or serving the fallback one
// Saves credentials to NVS (first choice from now on) and reconnects.
esp_err_t net_set_wifi(const char *ssid, const char *pass);
// JSON array of visible networks: [{"ssid":"...","rssi":-60,"open":false},...]
esp_err_t net_scan_json(char *out, size_t cap);
bool time_is_valid(void);

// ---- ota.c ----------------------------------------------------------------------------
struct httpd_req;
int ota_post_handler(struct httpd_req *req);  // POST /api/update
void ota_confirm_when_healthy(void);          // rollback guard for a freshly updated image
bool ota_busy(void);                          // receiving an update or restarting into it
int ota_wifi_post_handler(struct httpd_req *req); // POST /api/update-wifi (ESP32-C6 image)
void ota_log_coprocessor_version(void);
void examples_install(void);                  // first boot: example takes into flash
void logbuf_start(void);                       // copy log lines into a ring for /api/log
int log_get_handler(struct httpd_req *req);   // GET /api/log?since=N
int log_page_handler(struct httpd_req *req);  // GET /log

// ---- led.c ----------------------------------------------------------------------------
// Optional RGB status light (WS2812 on CONFIG_PL_LED_GPIO). Harmless when nothing is wired.
void led_start(void);

// ---- web.c ----------------------------------------------------------------------------
void web_start(void);
