#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico/cyw43_arch.h"

#define PI 3.14159265358979323846

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16 // no need for miso since dac will not be writing back 
#define PIN_CS_DAC   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_CS_RAM  20

#define SINE_SAMPLES 1024
#define SAMPLE_DELAY_US 0 // for max hz, but actual deplay is caused by the other part of the code 


static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0); // pull low 
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

void mcp4912_write(uint8_t channel, uint16_t value) {
    if (value > 1023) value = 1023; // capping 

    uint16_t command;

    if (channel == 0) {
        command = 0b0011000000000000;  // DAC A
    } else {
        command = 0b1011000000000000;  // DAC B
    }

    command |= value << 2;

    uint8_t data[2];
    data[0] = command >> 8;
    data[1] = command & 0xFF;

    cs_select(PIN_CS_DAC);
    spi_write_blocking(SPI_PORT, data, 2);
    cs_deselect(PIN_CS_DAC);
}


int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    // gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so initialise it high before writing.
    gpio_init(PIN_CS_DAC);
    gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    gpio_put(PIN_CS_DAC, 1);

    uint16_t sine_table[SINE_SAMPLES];
    for (int i = 0; i < SINE_SAMPLES; i++) {
        sine_table[i] = (uint16_t)(511.5 * (1.0 + sin(2.0 * PI * i / SINE_SAMPLES))); // 1024 / 2 = 512 so we are using the first half of the sine wave 
    }

    // Example to turn on the Pico W LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    while (true) {
        for (int i = 0; i < SINE_SAMPLES; i++) {
            mcp4912_write(0, sine_table[i]);
            sleep_us(SAMPLE_DELAY_US);
        }
    }
}
