// Host test for the status-light colours: gcc -I../main led_test.c ../main/led_pattern.c
#include <assert.h>
#include <stdio.h>
#include "led_pattern.h"

static pl_led_input_t base(void)
{
    pl_led_input_t in = {0};
    in.card = true;
    in.wifi = true;
    return in;
}

static int lit(pl_rgb_t c) { return c.r || c.g || c.b; }

int main(void)
{
    pl_led_input_t in = base();

    // Idle: steady, dim, warm white with a card and yellow (no blue) without.
    pl_rgb_t a = led_color(&in, 0, 60), b = led_color(&in, 1500, 60);
    assert(led_state(&in) == LED_IDLE && lit(a) && a.r == b.r && a.g == b.g && a.b == b.b);
    assert(a.r <= 12 && a.b > 0);
    in.card = false;
    pl_rgb_t y = led_color(&in, 0, 60);
    assert(y.r > 0 && y.g > 0 && y.b == 0);
    assert(y.r != a.r || y.g != a.g || y.b != a.b);

    // Recording wins over everything except updates and faults; bright red, steady.
    in = base();
    in.recording = true;
    in.wifi = false;
    in.card = false;
    pl_rgb_t r = led_color(&in, 0, 60), r2 = led_color(&in, 1500, 60);
    assert(led_state(&in) == LED_RECORDING && r.r == 60 && r.g == 0 && r.b == 0 && r2.r == 60);

    // No Wi-Fi: idle colour blinking slowly; setup network: cyan blinking slowly.
    in = base();
    in.wifi = false;
    assert(led_state(&in) == LED_NO_WIFI && lit(led_color(&in, 500, 60)) && !lit(led_color(&in, 1500, 60)));
    in.setup_ap = true;
    pl_rgb_t s = led_color(&in, 500, 60);
    assert(led_state(&in) == LED_SETUP && s.r == 0 && s.g > 0 && s.b > 0 && !lit(led_color(&in, 1500, 60)));
    in.wifi = true;                                  // AP still closing after joining: plain idle
    assert(led_state(&in) == LED_IDLE);

    // Update beats recording; fault beats recording; both blink fast.
    in = base();
    in.recording = true;
    in.updating = true;
    pl_rgb_t u = led_color(&in, 100, 60);
    assert(led_state(&in) == LED_UPDATING && u.b > u.r && !lit(led_color(&in, 300, 60)));
    in.updating = false;
    in.fault = true;
    assert(led_state(&in) == LED_FAULT && lit(led_color(&in, 100, 60)) && !lit(led_color(&in, 300, 60)));

    // Lowest brightness still lights every state's on-phase.
    in = base();
    assert(lit(led_color(&in, 0, 1)));
    in.card = false;
    assert(lit(led_color(&in, 0, 1)));

    printf("status light: idle/no-card/recording/no-wifi/setup/update/fault OK\n");
    return 0;
}
