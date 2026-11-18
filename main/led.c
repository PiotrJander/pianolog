// Optional status light: one WS2812-type RGB LED (or an SK6812) on a header GPIO.
//
// The LED only ever receives data, so a device without one behaves exactly the same: the pin
// toggles into nothing. CONFIG_PL_LED_GPIO = -1 turns the feature off entirely.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app.h"
#include "led_pattern.h"

static const char *TAG = "led";

#if CONFIG_PL_LED_GPIO >= 0
#include "led_strip.h"

#if CONFIG_PL_LED_RGB_ORDER
#define LED_COLOR_FMT LED_STRIP_COLOR_COMPONENT_FMT_RGB
#else
#define LED_COLOR_FMT LED_STRIP_COLOR_COMPONENT_FMT_GRB
#endif

static led_strip_handle_t s_strip;

static void read_inputs(pl_led_input_t *in)
{
    pl_rec_status_t rec;
    pl_net_status_t net;
    recorder_get_status(&rec);
    net_get_status(&net);
    in->updating = ota_busy();
    in->fault = !flash_ready();
    in->recording = rec.recording;
    in->card = storage_ready();
    in->wifi = net.sta_connected;
    in->setup_ap = net.ap_active;
}

static void led_task(void *arg)
{
    pl_rgb_t shown = {1, 2, 3};                    // forces the first refresh
    for (;;) {
        pl_led_input_t in;
        read_inputs(&in);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        pl_rgb_t c = led_color(&in, now_ms, CONFIG_PL_LED_BRIGHTNESS);
        if (c.r != shown.r || c.g != shown.g || c.b != shown.b) {
            led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b);
            led_strip_refresh(s_strip);
            shown = c;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void led_start(void)
{
    if (CONFIG_PL_LED_GPIO == CONFIG_PL_DIN_RX_GPIO) {
        ESP_LOGW(TAG, "status light disabled: GPIO%d is already used for DIN MIDI IN", CONFIG_PL_LED_GPIO);
        return;
    }
    led_strip_config_t strip = {
        .strip_gpio_num = CONFIG_PL_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_COLOR_FMT,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
    if (err != ESP_OK) {                           // never stop the recorder over a light
        ESP_LOGW(TAG, "status light unavailable: %s", esp_err_to_name(err));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    xTaskCreate(led_task, "led", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "status light on GPIO%d", CONFIG_PL_LED_GPIO);
}

#else
void led_start(void)
{
    ESP_LOGI(TAG, "status light disabled");
}
#endif
