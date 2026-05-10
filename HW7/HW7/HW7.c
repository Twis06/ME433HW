#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico/cyw43_arch.h"

// SPI0 pins connected to the MCP4912 dual DAC.
#define SPI_PORT spi0
#define PIN_CS_DAC   17
#define PIN_SCK  18
#define PIN_MOSI 19

// MCP4912 is a 10-bit DAC, so each channel accepts values from 0 to 1023.
#define DAC_MAX_VALUE 1023u

// Oscilloscope setup: DAC A -> scope channel 1 (X), DAC B -> scope channel 2 (Y).
// Put the scope in XY mode to see the drawing.
#define SPI_BAUD_HZ (1000 * 1000)
#define DRAW_LINE_STEPS 18
#define FAST_MOVE_STEPS 1
#define POINT_HOLD_US 35
#define FRAME_PAUSE_US 0

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef enum {
    DAC_CHANNEL_A = 0,
    DAC_CHANNEL_B = 1,
} dac_channel_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} scope_point_t;

typedef struct {
    int16_t x;
    int16_t y;
} vector_point_t;

// One continuous five-point star path. Coordinates are normalized to +/-100;
// each frame scales them around the DAC center for a smooth twinkle.
static const vector_point_t star_shape[] = {
    {0, 100},
    {22, 31},
    {95, 31},
    {36, -12},
    {59, -81},
    {0, -38},
    {-59, -81},
    {-36, -12},
    {-95, 31},
    {-22, 31},
    {0, 100},
};

static const int8_t pulse_table[] = {
    0, 6, 12, 18, 23, 27, 30, 32,
    33, 32, 30, 27, 23, 18, 12, 6,
    0, -6, -12, -18, -23, -27, -30, -32,
    -33, -32, -30, -27, -23, -18, -12, -6,
};

static scope_point_t current_point;
static bool has_current_point = false;

static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop");
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop");
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop");
}

static void mcp4912_write(dac_channel_t channel, uint16_t value) {
    if (value > DAC_MAX_VALUE) {
        value = DAC_MAX_VALUE;
    }

    // Config bits: unbuffered, 1x gain, output enabled.
    uint16_t command = (channel == DAC_CHANNEL_A) ? 0x3000 : 0xB000;
    command |= (uint16_t)(value << 2);

    uint8_t data[2] = {
        (uint8_t)(command >> 8),
        (uint8_t)(command & 0xFF),
    };

    cs_select(PIN_CS_DAC);
    spi_write_blocking(SPI_PORT, data, 2);
    cs_deselect(PIN_CS_DAC);
}

static void scope_write_xy(uint16_t x, uint16_t y) {
    mcp4912_write(DAC_CHANNEL_A, x);
    mcp4912_write(DAC_CHANNEL_B, y);
}

static uint16_t interpolate_channel(uint16_t start, uint16_t end, uint32_t step, uint32_t total_steps) {
    int32_t distance = (int32_t)end - (int32_t)start;
    return (uint16_t)((int32_t)start + distance * (int32_t)step / (int32_t)total_steps);
}

static uint16_t clamp_to_dac(int32_t value) {
    if (value < 0) {
        return 0;
    }

    if (value > DAC_MAX_VALUE) {
        return DAC_MAX_VALUE;
    }

    return (uint16_t)value;
}

static scope_point_t make_scope_point(int32_t x, int32_t y) {
    scope_point_t point = {
        .x = clamp_to_dac(x),
        .y = clamp_to_dac(y),
    };

    return point;
}

static scope_point_t make_static_point(vector_point_t point) {
    return make_scope_point(point.x, point.y);
}

static void write_interpolated(scope_point_t start, scope_point_t end, uint32_t steps, uint32_t hold_us) {
    for (uint32_t step = 1; step <= steps; step++) {
        uint16_t x = interpolate_channel(start.x, end.x, step, steps);
        uint16_t y = interpolate_channel(start.y, end.y, step, steps);

        scope_write_xy(x, y);
        if (hold_us > 0) {
            sleep_us(hold_us);
        }
    }

    current_point = end;
    has_current_point = true;
}

static void move_quickly_to(scope_point_t end) {
    if (!has_current_point) {
        scope_write_xy(end.x, end.y);
        current_point = end;
        has_current_point = true;
        return;
    }

    write_interpolated(current_point, end, FAST_MOVE_STEPS, 0);
}

static void draw_slowly_to(scope_point_t end) {
    write_interpolated(current_point, end, DRAW_LINE_STEPS, POINT_HOLD_US);
}

static void draw_stroke(const vector_point_t points[], size_t point_count) {
    if (point_count == 0) {
        return;
    }

    move_quickly_to(make_static_point(points[0]));

    for (size_t i = 1; i < point_count; i++) {
        draw_slowly_to(make_static_point(points[i]));
    }
}

static int16_t pulse_at(uint32_t frame, uint32_t phase) {
    size_t index = (frame + phase) & (ARRAY_LEN(pulse_table) - 1);
    return pulse_table[index];
}

static void draw_star(uint32_t frame) {
    has_current_point = false;

    int16_t scale = 285 + pulse_at(frame, 0);
    vector_point_t points[ARRAY_LEN(star_shape)];

    for (size_t i = 0; i < ARRAY_LEN(star_shape); i++) {
        points[i].x = 512 + (int16_t)((star_shape[i].x * scale) / 100);
        points[i].y = 512 + (int16_t)((star_shape[i].y * scale) / 100);
    }

    draw_stroke(points, ARRAY_LEN(points));
}

static void init_dac_spi(void) {
    spi_init(SPI_PORT, SPI_BAUD_HZ);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS_DAC);
    gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    gpio_put(PIN_CS_DAC, 1);
}

int main() {
    if (cyw43_arch_init()) {
        return -1;
    }

    init_dac_spi();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    uint32_t frame = 0;

    while (true) {
        draw_star(frame++);
        sleep_us(FRAME_PAUSE_US);
    }
}
