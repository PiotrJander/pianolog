#include "led_pattern.h"

pl_led_state_t led_state(const pl_led_input_t *in)
{
    if (in->updating)  return LED_UPDATING;
    if (in->fault)     return LED_FAULT;
    if (in->recording) return LED_RECORDING;       // what matters most while playing
    if (in->setup_ap && !in->wifi) return LED_SETUP;
    if (!in->wifi)     return LED_NO_WIFI;
    return LED_IDLE;
}

// Scales a full-intensity colour (components 0..255) to level 0..255, never rounding a lit
// component down to 0, so dim colours keep their hue.
static pl_rgb_t scale(uint8_t r, uint8_t g, uint8_t b, unsigned level)
{
    pl_rgb_t c;
    c.r = r ? (uint8_t)((r * level + 254) / 255) : 0;
    c.g = g ? (uint8_t)((g * level + 254) / 255) : 0;
    c.b = b ? (uint8_t)((b * level + 254) / 255) : 0;
    return c;
}

static pl_rgb_t idle_color(bool card, unsigned level)
{
    return card ? scale(255, 190, 120, level)     // warm white
                : scale(255, 150, 0, level);      // yellow: recording to internal memory only
}

pl_rgb_t led_color(const pl_led_input_t *in, uint32_t now_ms, uint8_t bright)
{
    static const pl_rgb_t off = {0, 0, 0};
    unsigned full = bright ? bright : 1;
    unsigned dim = full / 5 ? full / 5 : 1;
    bool fast_on = (now_ms % 400) < 200;           // 2.5 blinks per second
    bool slow_on = (now_ms % 2000) < 1000;         // one blink every 2 s

    switch (led_state(in)) {
    case LED_UPDATING:  return fast_on ? scale(0, 60, 255, full) : off;
    case LED_FAULT:     return fast_on ? scale(255, 0, 200, full) : off;
    case LED_RECORDING: return scale(255, 0, 0, full);
    case LED_SETUP:     return slow_on ? scale(0, 255, 200, dim) : off;
    case LED_NO_WIFI:   return slow_on ? idle_color(in->card, dim) : off;
    case LED_IDLE:
    default:            return idle_color(in->card, dim);
    }
}
