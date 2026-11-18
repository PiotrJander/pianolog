// pianolog — an always-on MIDI recorder for the Waveshare ESP32-P4-NANO.
//
//   piano --USB--> Type-A port --+
//                                +--> recorder --> flash /rec/<day>/<time>.mid --> card /rec/...
//   piano --DIN--> optocoupler --+                         \__________ web page __________/
//   optional RGB status light on a header GPIO (led.c)
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "app.h"

static const char *TAG = "pianolog";

static uint32_t bump_boot_count(void)
{
    uint32_t n = 0;
    nvs_handle_t h;
    if (nvs_open("pianolog", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &n);
        n++;
        nvs_set_u32(h, "boots", n);
        nvs_commit(h);
        nvs_close(h);
    }
    return n;
}

static void net_task(void *arg)
{
    net_start();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    uint32_t boots = bump_boot_count();
    ESP_LOGI(TAG, "pianolog " PL_VERSION ", boot #%u", (unsigned)boots);

    // On-board flash first: every take is recorded there, card or no card.
    flash_mount();
    recorder_start(boots);
    led_start();                                // optional status light
    recorder_repair_dir(PL_REC_DIR);

    if (storage_mount() == ESP_OK) {
        config_load();
        recorder_repair_dir(PL_CARD_REC);       // takes written by firmware 0.1 directly to the card
    }
    setenv("TZ", g_cfg.tz, 1);
    tzset();
    archive_start();

    midi_usb_start();
    midi_din_start();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    web_start();
    // Wi-Fi bring-up talks to the co-processor and may take a while (or hang if the C6
    // firmware is incompatible), so it gets its own task.
    xTaskCreate(net_task, "net_start", 6144, NULL, 4, NULL);
    ota_confirm_when_healthy();

    // Card pulled out or inserted late: keep trying to mount it.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!storage_ready() && storage_mount() == ESP_OK) {
            recorder_repair_dir(PL_CARD_REC);
            archive_kick();
        }
    }
}
