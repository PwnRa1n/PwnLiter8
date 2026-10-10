#include <stdio.h>
#include "pico/time.h"
#include "led.h"

#if LED_NEOPIXEL

#include "pico/status_led.h"

#if LED_RED_GREEN_SWAPPED
#define NEOPIXEL_RGB(r, g, b) \
    ((g << 16) | (r << 8) | (b))

#else
#define NEOPIXEL_RGB(r, g, b) \
    ((g << 8) | (r << 16) | (b))
#endif

#define DISABLED    NEOPIXEL_RGB(0, 0, 0)

/*
 * Pwnliter8 status palette at 50% brightness.
 *
 * Every 8-bit channel is scaled by the same factor, so the hue ratios stay
 * exactly as requested; 128/255 = 50.2% of the full scale the WS2812 driver
 * can express (a full-scale channel lands on 128, the nearest integer to
 * 255/2).
 *
 * Limits check: the driver addresses a SINGLE pixel (gpio range 1) on a
 * hard-wired rail (no PICO_DEFAULT_WS2812_POWER_PIN).  The worst colour
 * (Royal Purple) now draws ~16 mA, about half the ~50 mA white ceiling and
 * trivially inside a USB / onboard-regulator budget of 500 mA+.  A WS2812
 * frame is a fixed 24-bit / ~30 us transfer regardless of value, so
 * brightness changes the duty ratio only - never the write timing.
 */
#define NEO_SCALE(v)    (((v) * 128) / 255)

#define AMBER       NEOPIXEL_RGB(NEO_SCALE(0xff), NEO_SCALE(0xb0), NEO_SCALE(0x20))  /* #FFB020 */
#define CYAN        NEOPIXEL_RGB(NEO_SCALE(0x00), NEO_SCALE(0xd5), NEO_SCALE(0xff))  /* #00D5FF */
#define LIME        NEOPIXEL_RGB(NEO_SCALE(0xa3), NEO_SCALE(0xe6), NEO_SCALE(0x35))  /* #A3E635 */
#define PURPLE      NEOPIXEL_RGB(NEO_SCALE(0xa8), NEO_SCALE(0x55), NEO_SCALE(0xf7))  /* #A855F7 */
#define RED         NEOPIXEL_RGB(NEO_SCALE(0xff), NEO_SCALE(0x3b), NEO_SCALE(0x4f))  /* #FF3B4F */

#define SUCCESS_COLOR   PURPLE

extern void set_ws2812(uint32_t value);

void led_switch_color(uint32_t color) {
    set_ws2812(color);
}

void led_init(void) {
    status_led_init();
}

#elif LED_RGB_PWM

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PWM_WRAP    (256)
#define PWM_RGB(r, g, b) \
    ((r << 16) | (g << 8) | b)

#define DISABLED    PWM_RGB(0, 0, 0)

/*
 * Same palette at 50% duty cycle.  A full-scale channel lands on level 128,
 * and 128/256 = exactly 50% duty; every channel is scaled by the same factor
 * so the hue ratios stay as requested.
 *
 * Limits check: 50% is comfortably inside the board's design - led_init()
 * already requests level PWM_WRAP (100%) on all three channels before the
 * first colour is ever written, so even full duty would be within spec.
 */
#define PWM_SCALE(v)    (((v) * 128) / 255)

#define AMBER       PWM_RGB(PWM_SCALE(0xff), PWM_SCALE(0xb0), PWM_SCALE(0x20))  /* #FFB020 */
#define CYAN        PWM_RGB(PWM_SCALE(0x00), PWM_SCALE(0xd5), PWM_SCALE(0xff))  /* #00D5FF */
#define LIME        PWM_RGB(PWM_SCALE(0xa3), PWM_SCALE(0xe6), PWM_SCALE(0x35))  /* #A3E635 */
#define PURPLE      PWM_RGB(PWM_SCALE(0xa8), PWM_SCALE(0x55), PWM_SCALE(0xf7))  /* #A855F7 */
#define RED         PWM_RGB(PWM_SCALE(0xff), PWM_SCALE(0x3b), PWM_SCALE(0x4f))  /* #FF3B4F */

#define SUCCESS_COLOR   PURPLE

static struct {
    uint r_slice;
    uint r_chan;
    uint g_slice;
    uint g_chan;
    uint b_slice;
    uint b_chan;
} pwm_ctx = { 0 };

void led_switch_color(uint32_t color) {
    pwm_set_chan_level(pwm_ctx.r_slice, pwm_ctx.r_chan, (color >> 16) & 0xFF);
    pwm_set_chan_level(pwm_ctx.g_slice, pwm_ctx.g_chan, (color >> 8)  & 0xFF);
    pwm_set_chan_level(pwm_ctx.b_slice, pwm_ctx.b_chan, (color >> 0)  & 0xFF);
}

void led_init(void) {
    int r_pin = LED_RGB_RED_PIN, g_pin = LED_RGB_GREEN_PIN, b_pin = LED_RGB_BLUE_PIN;

    gpio_set_function(r_pin, GPIO_FUNC_PWM);
    gpio_set_function(g_pin, GPIO_FUNC_PWM);
    gpio_set_function(b_pin, GPIO_FUNC_PWM);

    pwm_ctx.r_slice = pwm_gpio_to_slice_num(r_pin);
    pwm_ctx.r_chan = pwm_gpio_to_channel(r_pin);

    pwm_ctx.g_slice = pwm_gpio_to_slice_num(g_pin);
    pwm_ctx.g_chan = pwm_gpio_to_channel(g_pin);

    pwm_ctx.b_slice = pwm_gpio_to_slice_num(b_pin);
    pwm_ctx.b_chan = pwm_gpio_to_channel(b_pin);

    pwm_set_wrap(pwm_ctx.r_slice, PWM_WRAP);
    pwm_set_wrap(pwm_ctx.g_slice, PWM_WRAP);
    pwm_set_wrap(pwm_ctx.b_slice, PWM_WRAP);

    pwm_set_chan_level(pwm_ctx.r_slice, pwm_ctx.r_chan, PWM_WRAP);
    pwm_set_chan_level(pwm_ctx.g_slice, pwm_ctx.g_chan, PWM_WRAP);
    pwm_set_chan_level(pwm_ctx.b_slice, pwm_ctx.b_chan, PWM_WRAP);

#if LED_RGB_ACTIVE_LOW
    // I hate this API
    pwm_set_output_polarity(pwm_ctx.r_slice, true, true);
    pwm_set_output_polarity(pwm_ctx.g_slice, true, true);
    pwm_set_output_polarity(pwm_ctx.b_slice, true, true);
#endif

    pwm_set_enabled(pwm_ctx.r_slice, true);
    pwm_set_enabled(pwm_ctx.g_slice, true);
    pwm_set_enabled(pwm_ctx.b_slice, true);
}

#elif LED_SINGLE_COLOR

#include "hardware/pwm.h"
#include "hardware/gpio.h"

#define PWM_WRAP    (768)

static struct {
    uint slice;
    uint chan;

    repeating_timer_t timer;
    bool blink_en;
    bool blink_curr_state;

    repeating_timer_t breathing_timer;
    uint breathing_level;
    bool breathing_falling;
} led_ctx = { 0 };

void led_init(void) {
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);

    led_ctx.slice = pwm_gpio_to_slice_num(LED_PIN);
    led_ctx.chan = pwm_gpio_to_channel(LED_PIN);

    pwm_set_wrap(led_ctx.slice, PWM_WRAP);
    pwm_set_chan_level(led_ctx.slice, led_ctx.chan, PWM_WRAP);
    pwm_set_enabled(led_ctx.slice, true);
}

#define ENABLED     (PWM_WRAP - 1)
#define DISABLED    (0)

void led_toggle(uint on) {
    pwm_set_chan_level(led_ctx.slice, led_ctx.chan, on);
}

bool _led_timer_cb(__unused repeating_timer_t *rt) {
    led_toggle(led_ctx.blink_curr_state ? ENABLED : DISABLED);
    led_ctx.blink_curr_state = !led_ctx.blink_curr_state;

    return true;
}

void led_set_blinking(uint64_t period_ms) {
    if (led_ctx.blink_en) {
        cancel_repeating_timer(&led_ctx.timer);
    }

    if (period_ms) {
        add_repeating_timer_ms(period_ms, _led_timer_cb, NULL, &led_ctx.timer);
        led_ctx.blink_en = true;
    } else {
        led_toggle(false);
        led_ctx.blink_en = false;
    }
}

bool _led_breathing_timer_cb(__unused repeating_timer_t *rt) {
    if (!led_ctx.breathing_falling) {
        led_ctx.breathing_level++;

        if (led_ctx.breathing_level == ENABLED) {
            led_ctx.breathing_falling = true;
        }
    } else {
        led_ctx.breathing_level--;

        if (led_ctx.breathing_level == 0) {
            led_ctx.breathing_falling = false;
        }
    }

    pwm_set_chan_level(led_ctx.slice, led_ctx.chan, led_ctx.breathing_level);

    return true;
}

void led_set_breathing(bool on) {
    if (on) {
        led_set_blinking(0);
        add_repeating_timer_ms(2, _led_breathing_timer_cb, NULL, &led_ctx.breathing_timer);
    } else {
        cancel_repeating_timer(&led_ctx.breathing_timer);
        led_toggle(DISABLED);
    }
}

#endif

#if LED_NEOPIXEL || LED_RGB_PWM

static struct {
    uint32_t color;
    repeating_timer_t timer;
    bool blink_en;
    bool blink_curr_state;

    /* one-shot power-on animation */
    repeating_timer_t startup_timer;
    bool startup_active;
    uint startup_idx;
} led_ctx = { 0 };

void led_set_color(uint32_t color) {
    led_ctx.color = color;
    led_switch_color(color);
}

bool _led_timer_cb(__unused repeating_timer_t *rt) {
    uint32_t color = 0;

    if (led_ctx.blink_curr_state) {
        color = led_ctx.color;
    } else {
        color = DISABLED;
    }

    led_switch_color(color);

    led_ctx.blink_curr_state = !led_ctx.blink_curr_state;

    return true;
}

void led_set_blinking(uint64_t period_ms) {
    if (led_ctx.blink_en) {
        cancel_repeating_timer(&led_ctx.timer);
    }

    if (period_ms) {
        add_repeating_timer_ms(period_ms, _led_timer_cb, NULL, &led_ctx.timer);
        led_ctx.blink_en = true;
    } else {
        led_switch_color(led_ctx.color);
        led_ctx.blink_en = false;
    }
}

/*
 * Power-on startup animation: show each palette colour once, in order, then
 * land on steady idle cyan.  It is driven from LED_STATE_BOOTING, i.e. it runs
 * strictly inside main()'s boot window -- the load-bearing sleep_ms(2000) that
 * precedes usb_start()/exploit_run() -- so the timer is always finished and
 * cancelled before USB, PIO and the exploit start.  led_stop_startup() is
 * additionally called on every state change, so no animation timer can outlive
 * a transition into RUNNING/SUCCESS/ERROR.
 *
 * Per step this does exactly what the pre-existing boot blink timer already did
 * in this same window (one set_ws2812()/pwm_set_chan_level() call), so it adds
 * no new load during boot and nothing at all while the exploit runs.
 */
static const uint32_t startup_colors[] = { AMBER, CYAN, LIME, PURPLE, RED };

#define STARTUP_STEPS   (sizeof(startup_colors) / sizeof(startup_colors[0]))
#define STARTUP_STEP_MS (150)

static bool _led_startup_cb(__unused repeating_timer_t *rt) {
    led_ctx.startup_idx++;

    if (led_ctx.startup_idx >= STARTUP_STEPS) {
        /* every colour shown: settle on steady idle cyan and stop repeating */
        led_ctx.startup_active = false;
        led_set_color(CYAN);
        return false;
    }

    led_switch_color(startup_colors[led_ctx.startup_idx]);
    return true;
}

static void led_start_startup(void) {
    led_ctx.startup_idx = 0;
    led_set_color(startup_colors[0]);   /* amber, shown immediately */

    if (add_repeating_timer_ms(STARTUP_STEP_MS, _led_startup_cb, NULL, &led_ctx.startup_timer)) {
        led_ctx.startup_active = true;
    } else {
        /* no timer available: skip the animation rather than stall on amber */
        led_set_color(CYAN);
    }
}

static void led_stop_startup(void) {
    if (led_ctx.startup_active) {
        cancel_repeating_timer(&led_ctx.startup_timer);
        led_ctx.startup_active = false;
    }
}

void led_set_state(int state) {
    /* a state change must never leave a startup timer behind */
    led_stop_startup();

    switch (state) {
        case LED_STATE_BOOTING: {
            led_start_startup();
            break;
        }

        case LED_STATE_IDLE: {
            led_set_color(CYAN);
            led_set_blinking(0);
            break;
        }

        case LED_STATE_RUNNING: {
            led_set_color(LIME);
            // blinking here breaks reliability, IDK
            led_set_blinking(0);
            break;
        }

        case LED_STATE_SUCCESS: {
            led_set_color(SUCCESS_COLOR);
            led_set_blinking(0);
            break;
        }

        case LED_STATE_ERROR: {
            led_set_color(RED);
            led_set_blinking(0);
            break;
        }
    }
}

#elif LED_SINGLE_COLOR

void led_set_state(int state) {
    switch (state) {
        case LED_STATE_BOOTING: {
            led_set_blinking(200);
            break;
        }

        case LED_STATE_IDLE: {
            led_set_breathing(true);
            break;
        }

        case LED_STATE_RUNNING: {
            led_set_breathing(false);
            led_set_blinking(100);
            break;
        }

        case LED_STATE_SUCCESS: {
            led_set_blinking(0);
            led_toggle(ENABLED);
            break;
        }

        case LED_STATE_ERROR: {
            led_set_blinking(0);
            led_toggle(DISABLED);
            break;
        }
    }
}

#else

void led_init(void) {
}

void led_set_state(int state) {
}

#endif
