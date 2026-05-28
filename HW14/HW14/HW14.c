#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"

#define HX711_SCK_PIN 18
#define HX711_DT_PIN 19
#define HX711_READY_TIMEOUT_US 200000
#define MAX_SAMPLES 4096
#define IIR_SHIFT 3

static int32_t raw_samples[MAX_SAMPLES];
static int32_t filtered_samples[MAX_SAMPLES];
static uint32_t time_samples_ms[MAX_SAMPLES];

static uint32_t read_sample_count(void)
{
    char buffer[16];
    int index = 0;

    while (true) {
        int ch = getchar_timeout_us(100000);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (index > 0) {
                buffer[index] = '\0';
                return strtoul(buffer, NULL, 10);
            }
            continue;
        }

        if (ch >= '0' && ch <= '9' && index < (int) sizeof(buffer) - 1) {
            buffer[index++] = (char) ch;
            putchar(ch);
            fflush(stdout);
        }
    }
}

static void hx711_init(void)
{
    gpio_init(HX711_SCK_PIN);
    gpio_set_dir(HX711_SCK_PIN, GPIO_OUT);
    gpio_put(HX711_SCK_PIN, 0);

    gpio_init(HX711_DT_PIN);
    gpio_set_dir(HX711_DT_PIN, GPIO_IN);
}

static bool hx711_read(int32_t *value)
{
    uint32_t raw = 0;
    absolute_time_t timeout = make_timeout_time_us(HX711_READY_TIMEOUT_US);

    while (gpio_get(HX711_DT_PIN)) {
        if (absolute_time_diff_us(get_absolute_time(), timeout) <= 0) {
            return false;
        }
        tight_loop_contents();
    }

    uint32_t interrupts = save_and_disable_interrupts();

    for (int i = 0; i < 24; i++) {
        gpio_put(HX711_SCK_PIN, 1);
        busy_wait_us_32(1);
        raw = (raw << 1) | gpio_get(HX711_DT_PIN);
        gpio_put(HX711_SCK_PIN, 0);
        busy_wait_us_32(1);
    }

    // One extra pulse selects channel A with gain 128 for the next conversion.
    gpio_put(HX711_SCK_PIN, 1);
    busy_wait_us_32(1);
    gpio_put(HX711_SCK_PIN, 0);

    restore_interrupts(interrupts);

    // Sign-extend 24-bit two's complement to 32-bit signed int.
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }

    *value = (int32_t) raw;
    return true;
}

int main()
{
    stdio_init_all();
    hx711_init();

    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Example to turn on the Pico W LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    while (true) {
        printf("Enter sample count, max %u:\n", MAX_SAMPLES);
        fflush(stdout);

        uint32_t requested_samples = read_sample_count();
        printf("\n");

        if (requested_samples > MAX_SAMPLES) {
            requested_samples = MAX_SAMPLES;
        }

        printf("Collecting %lu samples\n", requested_samples);
        fflush(stdout);

        int32_t filtered = 0;
        bool have_filtered = false;

        for (uint32_t i = 0; i < requested_samples; i++) {
            int32_t raw = 0;

            if (!hx711_read(&raw)) {
                gpio_put(HX711_SCK_PIN, 0);
                raw = 0;
            }

            if (!have_filtered) {
                filtered = raw;
                have_filtered = true;
            } else {
                filtered += (raw - filtered) >> IIR_SHIFT;
            }

            time_samples_ms[i] = to_ms_since_boot(get_absolute_time());
            raw_samples[i] = raw;
            filtered_samples[i] = filtered;
        }

        printf("BEGIN %lu\n", requested_samples);
        printf("time_ms,raw,filtered\n");
        for (uint32_t i = 0; i < requested_samples; i++) {
            printf("%lu,%ld,%ld\n", time_samples_ms[i], raw_samples[i], filtered_samples[i]);
        }
        printf("END\n");
        fflush(stdout);
    }
}
