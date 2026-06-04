#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

#define I2C_PORT i2c1
#define I2C_SDA 26
#define I2C_SCL 27
#define AS5600_ADDR 0x36
#define AS5600_ANGLE 0x0E
#define PLOT_W 60

static bool as5600_read_angle(uint16_t *angle)
{
    uint8_t reg = AS5600_ANGLE;
    uint8_t data[2];

    if (i2c_write_blocking(I2C_PORT, AS5600_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    if (i2c_read_blocking(I2C_PORT, AS5600_ADDR, data, 2, false) != 2) {
        return false;
    }

    *angle = ((data[0] << 8) | data[1]) & 0x0FFF;
    return true;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    while (true) {
        uint16_t raw;
        if (as5600_read_angle(&raw)) {
            float deg = raw * 360.0f / 4096.0f;
            int pos = raw * PLOT_W / 4096;

            printf("\r%6.1f deg |", deg);
            for (int i = 0; i < PLOT_W; i++) {
                putchar(i == pos ? '*' : '-');
            }
            printf("|   ");
        } else {
            printf("\rAS5600 not found at 0x%02X   ", AS5600_ADDR);
        }
        sleep_ms(50);
    }
}
