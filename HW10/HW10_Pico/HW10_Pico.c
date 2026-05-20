#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define I2C_PORT i2c1
#define I2C_SDA 18
#define I2C_SCL 19
#define BUTTON_PIN 15

#define MPU6050_ADDR 0x68
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_YOUT_H 0x3D

static bool imu_write_u8(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_write_blocking(I2C_PORT, MPU6050_ADDR, data, 2, false) == 2;
}

static bool imu_read_i16(uint8_t reg, int16_t *value) {
    uint8_t data[2];
    if (i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    if (i2c_read_blocking(I2C_PORT, MPU6050_ADDR, data, 2, false) != 2) {
        return false;
    }

    *value = (int16_t)((data[0] << 8) | data[1]);
    return true;
}

int main() {
    stdio_init_all();

    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    bool imu_ready = imu_write_u8(MPU6050_PWR_MGMT_1, 0x00);

    while (true) {
        int16_t raw_y = 0;
        float accel_y = 0.0f;
        int button_pressed = gpio_get(BUTTON_PIN) == 0;

        if (imu_ready && imu_read_i16(MPU6050_ACCEL_YOUT_H, &raw_y)) {
            accel_y = raw_y / 16384.0f;
        } else {
            imu_ready = imu_write_u8(MPU6050_PWR_MGMT_1, 0x00);
        }

        printf("%.4f,%d\n", accel_y, button_pressed);
        sleep_ms(20);
    }
}
