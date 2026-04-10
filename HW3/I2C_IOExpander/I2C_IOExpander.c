// #include <stdio.h>
// #include "pico/stdlib.h"
// #include "hardware/i2c.h"
// #include "pico/cyw43_arch.h"


// // I2C defines
// // This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
// // Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
// #define I2C_PORT i2c0
// #define I2C_SDA 4
// #define I2C_SCL 5



// int main()
// {
//     stdio_init_all();
//     sleep_ms(20);
//     if (cyw43_arch_init()) {
//         printf("Wi-Fi init failed");
//         return -1;
//     }
//     // I2C Initialisation. Using it at 400Khz.
//     i2c_init(I2C_PORT, 400*1000);
    
//     gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA);
//     gpio_pull_up(I2C_SCL);

//     while (true) {
//         printf("Hello, world!\n");
//         cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
//         sleep_ms(500);
//         cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
//         sleep_ms(500);
//     }
// }
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

// ---------------- I2C CONFIG ----------------
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5

#define MCP_ADDR 0x20

// ---------------- MCP23008 REGISTERS ----------------
#define IODIR  0x00
#define GPPU   0x06
#define GPIO   0x09
#define OLAT   0x0A

// ---------------- I2C HELPERS ----------------
void mcp_write(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    i2c_write_blocking(I2C_PORT, MCP_ADDR, buf, 2, false);
}

uint8_t mcp_read(uint8_t reg) {
    uint8_t data;
    i2c_write_blocking(I2C_PORT, MCP_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MCP_ADDR, &data, 1, false);
    return data;
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("Starting MCP23008 test...\n");

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Init I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    printf("I2C initialized\n");

    // GP1 = input, GP7 = output
    // 1=input, 0=output
    mcp_write(IODIR, 0b00000010);

    // Enable pull-up on GP1
    mcp_write(GPPU, 0b00000010);

    // Start LED off
    mcp_write(OLAT, 0b00000000);

    printf("MCP23008 configured\n");

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(100);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        uint8_t gpio_state = mcp_read(GPIO);
        printf("GPIO state: 0x%02X\n", gpio_state);

        uint8_t button = gpio_state & 0b00000010;   // GP1

        if (button == 0) {
            printf("Button PRESSED -> LED ON\n");
            mcp_write(OLAT, 0b10000000);  // GP7 high
        } else {
            printf("Button released -> LED OFF\n");
            mcp_write(OLAT, 0b00000000);
        }

        sleep_ms(300);
    }
}