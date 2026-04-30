#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define I2C_PORT i2c_default
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_BAUD 2560000

#define MPU6050_ADDR 0x68

#define GYRO_CONFIG 0x1B
#define ACCEL_CONFIG 0x1C
#define PWR_MGMT_1 0x6B

#define ACCEL_XOUT_H 0x3B
#define WHO_AM_I 0x75

#define ACCEL_FS_2G 0x00
#define GYRO_FS_2000DPS 0x18
#define MOUSE_DEADZONE 1200
#define BUTTON_PIN 15

typedef enum {
    MODE_REGULAR = 0,
    MODE_REMOTE = 1
} mouse_mode_t;

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
} mpu6050_raw_t;

static int8_t mouse_dx = 0;
static int8_t mouse_dy = 0;
static mouse_mode_t mouse_mode = MODE_REGULAR;

static void imu_error_loop(void) {
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

static void mpu6050_init(void) {
    uint8_t who = imu_read_reg(WHO_AM_I);
    printf("WHO_AM_I = 0x%02X\n", who);

    if (who != 0x68 && who != 0x98) {
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

static int8_t accel_to_mouse_delta(int16_t raw_accel) {
    int sign = 1;
    int accel = raw_accel;

    if (accel < 0) {
        sign = -1;
        accel = -accel;
    }

    if (accel < MOUSE_DEADZONE) {
        return 0;
    }

    // 4 simple speed levels based on acceleration size.
    if (accel < 4000) return sign * 1;
    if (accel < 8000) return sign * 3;
    if (accel < 12000) return sign * 6;
    return sign * 10;
}

static void regular_mouse_mode(mpu6050_raw_t raw) {
    mouse_dx = accel_to_mouse_delta(raw.ax);
    mouse_dy = accel_to_mouse_delta(raw.ay);
}

static void remote_mouse_mode(void) {
    static uint32_t last_move_ms = 0;
    static int step = 0;
    static const int8_t circle_dx[] = {8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3, 0, 3, 6, 7};
    static const int8_t circle_dy[] = {0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3};

    if (board_millis() - last_move_ms < 62) {
        mouse_dx = 0;
        mouse_dy = 0;
        return;
    }

    last_move_ms = board_millis();
    mouse_dx = circle_dx[step];
    mouse_dy = circle_dy[step];
    step = (step + 1) % 16;
}

static void check_mode_button(void) {
    static bool last_pressed = false;
    static uint32_t last_toggle_ms = 0;

    bool pressed = !gpio_get(BUTTON_PIN);

    if (pressed && !last_pressed && board_millis() - last_toggle_ms > 250) {
        if (mouse_mode == MODE_REGULAR) {
            mouse_mode = MODE_REMOTE;
        } else {
            mouse_mode = MODE_REGULAR;
        }

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, mouse_mode == MODE_REMOTE);
        last_toggle_ms = board_millis();
    }

    last_pressed = pressed;
}

static void send_hid_report(uint8_t report_id, uint32_t btn) {
    if (!tud_hid_ready()) {
        return;
    }

    switch (report_id) {
        case REPORT_ID_MOUSE:
            tud_hid_mouse_report(REPORT_ID_MOUSE, btn, mouse_dx, mouse_dy, 0, 0);
            break;

        default:
            break;
    }
}

static void hid_task(void) {
    const uint32_t interval_ms = 1;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms) {
        return;
    }
    start_ms += interval_ms;

    uint32_t btn = 0;
    send_hid_report(REPORT_ID_MOUSE, btn); // was REPORT_ID_KEYBOARD
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    if (cyw43_arch_init()) {
        return -1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    mpu6050_init();

    printf("Using raw MPU6050 accelerometer data to control USB mouse.\n");

    while (true) {
        tud_task();
        check_mode_button();

        mpu6050_raw_t raw;
        mpu6050_read_raw(&raw);

        if (mouse_mode == MODE_REGULAR) {
            regular_mouse_mode(raw);
        } else {
            remote_mouse_mode();
        }

        hid_task();

        tight_loop_contents();
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
