// Over-the-air firmware updates.
//
//   curl -H "X-Update-Token: <token>" --data-binary @build/pianolog.bin http://pianolog.local/api/update
//
// The image is streamed into the inactive app slot (ota_0 / ota_1) and checked by
// esp_ota_end() (magic, chip, revision, SHA-256 of the whole image) before the boot slot is
// switched. With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE the new image starts in "pending
// verify" state: ota_confirm_when_healthy() marks it valid only once the flash file system
// is mounted and the network is up. If it crashes or never gets that far, the bootloader
// returns to the previous image on the next reset.
//
// The token travels in clear text over plain HTTP: it keeps strangers and mistakes out, not
// someone who can sniff your Wi-Fi.
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app.h"

static const char *TAG = "ota";
static volatile bool s_busy;

bool ota_busy(void) { return s_busy; }

#define HEALTH_TIMEOUT_S 120

static bool token_ok(httpd_req_t *req)
{
    const char *want = CONFIG_PL_OTA_TOKEN;
    if (!want[0]) return false;                     // updates disabled
    char got[129] = "";
    if (httpd_req_get_hdr_value_str(req, "X-Update-Token", got, sizeof got) != ESP_OK) return false;
    size_t lw = strlen(want), lg = strlen(got);
    unsigned diff = (unsigned)(lw ^ lg);            // constant time for equal lengths
    for (size_t i = 0; i < lg; i++) diff |= (unsigned)(got[i] ^ want[i % (lw ? lw : 1)]);
    return diff == 0;
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, msg);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));                 // let the HTTP response leave
    recorder_finish_for_restart(3000);              // close the take being recorded, if any
    storage_unmount();
    ESP_LOGI(TAG, "restarting into the new firmware");
    esp_restart();
}

int ota_post_handler(httpd_req_t *req)
{
    if (!token_ok(req)) {
        vTaskDelay(pdMS_TO_TICKS(1000));            // slow down guessing
        return reply(req, "401 Unauthorized", "missing or wrong X-Update-Token\n");
    }
    if (s_busy) return reply(req, "409 Conflict", "an update is already running\n");

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return reply(req, "500 Internal Server Error", "no OTA slot (old partition table? flash once by cable)\n");
    if (req->content_len == 0 || req->content_len > part->size)
        return reply(req, "413 Payload Too Large", "image is empty or larger than the app slot\n");

    s_busy = true;
    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        s_busy = false;
        return reply(req, "500 Internal Server Error", "esp_ota_begin failed\n");
    }
    ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)req->content_len, part->label);

    char *buf = malloc(4096);
    size_t left = req->content_len;
    int timeouts = 0;
    while (buf && left > 0 && err == ESP_OK) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (r <= 0) { err = ESP_FAIL; break; }
        timeouts = 0;
        err = esp_ota_write(h, buf, (size_t)r);
        left -= (size_t)r;
    }
    free(buf);
    if (!buf || err != ESP_OK) {
        esp_ota_abort(h);
        s_busy = false;
        ESP_LOGW(TAG, "upload failed: %s", esp_err_to_name(err));
        return reply(req, "400 Bad Request", "upload interrupted or write failed; the running firmware is unchanged\n");
    }
    err = esp_ota_end(h);                           // verifies the whole image
    if (err != ESP_OK) {
        s_busy = false;
        ESP_LOGW(TAG, "image rejected: %s", esp_err_to_name(err));
        return reply(req, "400 Bad Request",
                     err == ESP_ERR_OTA_VALIDATE_FAILED
                         ? "not a valid image for this chip (wrong file, or wrong chip-revision build?)\n"
                         : "image check failed\n");
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(part, &desc) == ESP_OK)
        ESP_LOGI(TAG, "new image: %s %s, built %s %s", desc.project_name, desc.version, desc.date, desc.time);
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        s_busy = false;
        return reply(req, "500 Internal Server Error", "could not switch boot slot\n");
    }
    reply(req, "200 OK", "ok, restarting; if the new firmware does not come up it rolls back by itself\n");
    xTaskCreate(restart_task, "ota_restart", 3072, NULL, 5, NULL);
    return ESP_OK;
}

// ---- rollback guard ---------------------------------------------------------------------

static void health_task(void *arg)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)HEALTH_TIMEOUT_S * 1000000;
    while (esp_timer_get_time() < deadline) {
        if (flash_ready() && net_is_up()) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "new firmware confirmed");
            vTaskDelete(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGE(TAG, "new firmware did not reach the network in %d s, rolling back", HEALTH_TIMEOUT_S);
    recorder_finish_for_restart(3000);
    esp_ota_mark_app_invalid_rollback_and_reboot();
    vTaskDelete(NULL);
}

void ota_confirm_when_healthy(void)
{
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "running a fresh update from %s, waiting for health check", run->label);
        xTaskCreate(health_task, "ota_health", 3072, NULL, 4, NULL);
    }
}
