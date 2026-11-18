// Status light: which colour to show for the recorder's state. Pure logic, no ESP-IDF, so it
// is unit-tested on the host (test/led_test.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool updating;          // a firmware update is being received or the device is restarting into it
    bool fault;             // internal memory failed to mount: nothing can be recorded
    bool recording;         // a take is open
    bool card;              // microSD card mounted
    bool wifi;              // joined a Wi-Fi network
    bool setup_ap;          // the recorder's own setup network is up (Wi-Fi not configured or not found)
} pl_led_input_t;

typedef struct { uint8_t r, g, b; } pl_rgb_t;

typedef enum {
    LED_UPDATING,           // blue, fast blink
    LED_FAULT,              // magenta, fast blink
    LED_RECORDING,          // red, steady
    LED_SETUP,              // cyan, slow blink
    LED_NO_WIFI,            // idle colour, slow blink
    LED_IDLE,               // dim warm white with a card, dim yellow without
} pl_led_state_t;

pl_led_state_t led_state(const pl_led_input_t *in);

// Colour at time now_ms. `bright` is the full brightness (1..255) used while recording;
// idle colours are about a fifth of it.
pl_rgb_t led_color(const pl_led_input_t *in, uint32_t now_ms, uint8_t bright);
