#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "ssd1306.h"
#include "font.h"
#include "hardware/adc.h"

#define I2C_PORT i2c_default
#define I2C_SDA 8
#define I2C_SCL 9



static void draw_char(int x, int y, char c) {
    int column;
    int row;

    for (column = 0; column < 5; column++) {
        char column_bits = ASCII[c - 0x20][column];

        for (row = 0; row < 8; row++) {
            int pixel_on = (column_bits >> row) & 0x01;
            ssd1306_drawPixel(x + column, y + row, pixel_on);
        }
    }
    for (row = 0; row < 8; row++) { // space between 
        ssd1306_drawPixel(x + 5, y + row, 0);
    }
}

static void draw_string(int x, int y, const char *message) {
    while (*message) {
        draw_char(x, y, *message);
        x += 6; // 5 pixel character + 1 pixel spacing
        message++;
    }

    
}

int main() {
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    char message[32];
    char hz[32];

    stdio_init_all();

    // HeartBeast LED 
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Set up I2C
    i2c_init(I2C_PORT, 500 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_setup();
    ssd1306_clear();
    int i = 0;
    int start_time = to_us_since_boot(get_absolute_time());
    while (true) {
        int passed_time = to_us_since_boot(get_absolute_time()) - start_time;
        start_time = to_us_since_boot(get_absolute_time());
        ssd1306_clear();
        // Voltage reading
        uint16_t raw = adc_read();
        float voltage = raw * 3.3f / 4095.0f;
        // Refresh Rate 
        int refresh_rate = 1000000 / passed_time;
       
        sprintf(message, "Test Msg %d", i++);
        draw_string(0, 0, message);
        sprintf(message, "ADC Voltage: %.2f V", voltage);
        draw_string(0, 8, message);
        sprintf(hz, "Refresh Rate: %d Hz", refresh_rate);
        draw_string(0, 16, hz);
        draw_string(0, 24, "ME433 HW4"); // since letter is 8 pixels tall, and display is 32 pixels tall. 
        ssd1306_update(); // Push the pixel buffer to the display

        // heartbeat 
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(0.1);
        
    }
}
