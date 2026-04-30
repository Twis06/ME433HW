#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "ssd1306.h"

#define I2C_PORT i2c_default
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_BAUD 400000

#define MPU6050_ADDR 0x68

#define GYRO_CONFIG 0x1B
#define ACCEL_CONFIG 0x1C
#define PWR_MGMT_1 0x6B

#define ACCEL_XOUT_H 0x3B
#define WHO_AM_I 0x75

#define ACCEL_FS_2G 0x00
#define GYRO_FS_2000DPS 0x18

#define ACCEL_SCALE_G 0.000061f

#define OLED_WIDTH 128
#define OLED_HEIGHT 32

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
} mpu6050_raw_t;

static void imu_error_loop(void) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    while (true) {
        tight_loop_contents();
    }
}

static void imu_write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    if (i2c_write_blocking(I2C_PORT, MPU6050_ADDR, data, 2, false) != 2) {
        imu_error_loop();
    }
}

static uint8_t imu_read_reg(uint8_t reg) {
    uint8_t value = 0;

    if (i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true) != 1) {
        imu_error_loop();
    }
    if (i2c_read_blocking(I2C_PORT, MPU6050_ADDR, &value, 1, false) != 1) {
        imu_error_loop();
    }

    return value;
}

static void imu_read_burst(uint8_t reg, uint8_t *data, size_t len) {
    if (i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true) != 1) {
        imu_error_loop();
    }
    if (i2c_read_blocking(I2C_PORT, MPU6050_ADDR, data, len, false) != (int)len) {
        imu_error_loop();
    }
}

static int16_t combine_bytes(uint8_t high, uint8_t low) {
    return (int16_t)((high << 8) | low);
}

static void draw_emoji(int x, int y) {
    ssd1306_drawPixel(x - 1, y - 1, 1);
    ssd1306_drawPixel(x + 1, y - 1, 1);
    ssd1306_drawPixel(x - 1, y + 1, 1);
    ssd1306_drawPixel(x, y + 2, 1);
    ssd1306_drawPixel(x + 1, y + 1, 1);
}

static void draw_box(int x, int y) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            ssd1306_drawPixel(x + dx, y + dy, 1);
        }
    }
}

static void mpu6050_init(void) {
    uint8_t who = imu_read_reg(WHO_AM_I);
    printf("WHO_AM_I = 0x%02X\n", who);

    if (who != 0x68 && who != 0x98 ) {
        printf("IMU not detected. Expected 0x68 or 0x98\n");
        imu_error_loop();
    }

    imu_write_reg(PWR_MGMT_1, 0x00);
    imu_write_reg(ACCEL_CONFIG, ACCEL_FS_2G);
    imu_write_reg(GYRO_CONFIG, GYRO_FS_2000DPS);
}

static void mpu6050_read_raw(mpu6050_raw_t *raw) {
    uint8_t data[6];
    imu_read_burst(ACCEL_XOUT_H, data, sizeof(data));

    raw->ax = combine_bytes(data[0], data[1]);
    raw->ay = combine_bytes(data[2], data[3]);
    raw->az = combine_bytes(data[4], data[5]);
}

static float clamp(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    if (cyw43_arch_init()) {
        return -1;
    }

    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_setup();
    mpu6050_init();
    float target_x = OLED_WIDTH / 2.0f;
    float target_y = OLED_HEIGHT / 2.0f;
    const float step_size = 2.0f;

    while (true) {
        mpu6050_raw_t raw;

        mpu6050_read_raw(&raw);

        float ax_g = raw.ax * ACCEL_SCALE_G;
        float ay_g = raw.ay * ACCEL_SCALE_G;
        float az_g = raw.az * ACCEL_SCALE_G;
        float x_projection = clamp(ax_g, -1.0f, 1.0f);
        float y_projection = clamp(ay_g, -1.0f, 1.0f);

        target_x += x_projection * step_size;
        target_x = clamp(target_x, 1, OLED_WIDTH - 2);
        target_y -= y_projection * step_size;
        target_y = clamp(target_y, 1, OLED_HEIGHT - 2);

        printf("projected gravity (g): x=% .3f y=% .3f z=% .3f\n",
               ax_g, ay_g, az_g);

        ssd1306_clear();
        // draw_emoji((int)target_x, target_y);
        draw_box((int)target_x, target_y);
        ssd1306_update();

        sleep_ms(10);
    }
}
