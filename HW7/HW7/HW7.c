#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pico/cyw43_arch.h"

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS_DAC   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_CS_RAM  20

static void spi_ram_init(){
    // RAM initialisation sequence goes here
    int data[2];
    data[0] = 0b0000010; // example command
    data[1] = 0b0000000;
    cs_select(PIN_CS_RAM);
    spi_write_blocking(SPI_PORT, data, 2); // where data is a uint8_t array with length 2
    cs_deselect(PIN_CS_RAM);
}

void spi_ram_write(uint16_t address, uint8_t * value, int len){
    // RAM write sequence goes here
    // address is 16 bits, and the packet is 8 bits array. 
    uint8_t packet[5];
    packet[0] = 0b00000010;// instruction for write 
    packet[1] = (address >> 8); // high byte of address, only keeping the first 8 bits 
    packet[2] = (address & 0xFF); // low byte of address
    for(int i = 0; i < len; i++){
        packet[3 + i] = value[i];
    }
    cs_select(PIN_CS_RAM);
    spi_write_blocking(SPI_PORT, packet, 5); // where data is a uint8_t array with length 5
    cs_deselect(PIN_CS_RAM);
}

void ram_write_sine(){

    for (i = 0; i< 1024; i++){

        uint8_t value = (uint8_t)(127.5 * (1 + sin(2 * M_PI * i / 1024))); // example sine wave values
        spi_ram_write(i, &value, 1);
    }
}

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
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS_DAC,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // // Chip select is active-low, so we'll initialise it to a driven-high state
    // gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    // gpio_put(PIN_CS_DAC, 1);
    // // For more examples of SPI use see https://github.com/raspberrypi/pico-examples/tree/master/spi

    cs_select(PIN_CS_DAC);
    spi_write_blocking(SPI_PORT, data, len); // where data is a uint8_t array with length len
    cs_deselect(PIN_CS_DAC);

    // Example to turn on the Pico W LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
