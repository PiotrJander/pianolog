// 5-pin DIN MIDI IN: optocoupler output -> UART1 RX at 31250 baud.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app.h"
#include "midi_parse.h"

#if CONFIG_PL_DIN_RX_GPIO >= 0

static const char *TAG = "din";
#define DIN_UART UART_NUM_1

static void on_msg(void *ctx, int64_t t_us, const uint8_t *msg, size_t len)
{
    recorder_submit(SRC_DIN, t_us, msg, len);
}

static void din_task(void *arg)
{
    static midi_parser_t parser;
    midi_parser_init(&parser, on_msg, NULL);
    uint8_t buf[64];
    for (;;) {
        // Block for the first byte, then drain whatever else has already arrived.
        int n = uart_read_bytes(DIN_UART, buf, 1, portMAX_DELAY);
        if (n <= 0) continue;
        int64_t t = esp_timer_get_time();
        size_t more = 0;
        uart_get_buffered_data_len(DIN_UART, &more);
        if (more > sizeof buf - 1) more = sizeof buf - 1;
        if (more) {
            int m = uart_read_bytes(DIN_UART, buf + 1, more, 0);
            if (m > 0) n += m;
        }
        midi_parser_feed(&parser, t, buf, (size_t)n);
    }
}

void midi_din_start(void)
{
    const uart_config_t cfg = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(DIN_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(DIN_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(DIN_UART, UART_PIN_NO_CHANGE, CONFIG_PL_DIN_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_set_pull_mode(CONFIG_PL_DIN_RX_GPIO, GPIO_PULLUP_ONLY);    // idle-high when nothing is wired
    uart_set_rx_full_threshold(DIN_UART, 1);                        // interrupt per byte: lowest latency
    uart_set_rx_timeout(DIN_UART, 1);
    xTaskCreate(din_task, "midi_din", 3072, NULL, 7, NULL);
    ESP_LOGI(TAG, "DIN MIDI IN on GPIO%d", CONFIG_PL_DIN_RX_GPIO);
}

#else
void midi_din_start(void) {}
#endif
