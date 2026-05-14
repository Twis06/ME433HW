#include <math.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS_DAC 17
#define PIN_SCK 18
#define PIN_MOSI 19
#define PIN_CS_RAM 20

#define RAM_CMD_WRMR 0x01
#define RAM_CMD_WRITE 0x02
#define RAM_CMD_READ 0x03
#define RAM_MODE_SEQ 0x40

#define SPI_BAUD_HZ (1000 * 1000)
#define SINE_SAMPLES 1000
#define BYTES_PER_SAMPLE 2
#define DAC_MAX_VALUE 1023u
#define PI 3.14159265358979323846f

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

static void spi_ram_init(void) {
    uint8_t data[2] = {RAM_CMD_WRMR, RAM_MODE_SEQ};

    cs_select(PIN_CS_RAM);
    spi_write_blocking(SPI_PORT, data, sizeof(data));
    cs_deselect(PIN_CS_RAM);
}

static void spi_ram_write(uint16_t address, const uint8_t data[BYTES_PER_SAMPLE]) {
    uint8_t packet[3] = {
        RAM_CMD_WRITE,
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xFF),
    };

    cs_select(PIN_CS_RAM);
    spi_write_blocking(SPI_PORT, packet, sizeof(packet));
    spi_write_blocking(SPI_PORT, data, BYTES_PER_SAMPLE);
    cs_deselect(PIN_CS_RAM);
}

static void spi_ram_read(uint16_t address, uint8_t data[BYTES_PER_SAMPLE]) {
    uint8_t packet[3] = {
        RAM_CMD_READ,
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xFF),
    };

    cs_select(PIN_CS_RAM);
    spi_write_blocking(SPI_PORT, packet, sizeof(packet));
    spi_read_blocking(SPI_PORT, 0, data, BYTES_PER_SAMPLE);
    cs_deselect(PIN_CS_RAM);
}

static uint16_t make_dac_command(float voltage) {
    uint16_t dac_value = (uint16_t)((voltage * DAC_MAX_VALUE / 3.3f) + 0.5f);

    if (dac_value > DAC_MAX_VALUE) {
        dac_value = DAC_MAX_VALUE;
    }

    return (uint16_t)(0x3000 | (dac_value << 2));
}

static void write_sine_to_ram(void) {
    float sine[SINE_SAMPLES];

    for (uint16_t i = 0; i < SINE_SAMPLES; i++) {
        sine[i] = 1.65f * (sinf(2.0f * PI * (float)i / (float)SINE_SAMPLES) + 1.0f);
    }

    for (uint16_t i = 0; i < SINE_SAMPLES; i++) {
        uint16_t dac_command = make_dac_command(sine[i]);
        uint8_t data[BYTES_PER_SAMPLE] = {
            (uint8_t)(dac_command >> 8),
            (uint8_t)(dac_command & 0xFF),
        };

        spi_ram_write((uint16_t)(i * BYTES_PER_SAMPLE), data);
    }
}

static void spi_dac_write(const uint8_t data[BYTES_PER_SAMPLE]) {
    cs_select(PIN_CS_DAC);
    spi_write_blocking(SPI_PORT, data, BYTES_PER_SAMPLE);
    cs_deselect(PIN_CS_DAC);
}

static void init_spi(void) {
    spi_init(SPI_PORT, SPI_BAUD_HZ);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS_DAC);
    gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    gpio_put(PIN_CS_DAC, 1);

    gpio_init(PIN_CS_RAM);
    gpio_set_dir(PIN_CS_RAM, GPIO_OUT);
    gpio_put(PIN_CS_RAM, 1);
}

int main(void) {
    stdio_init_all();
    init_spi();
    spi_ram_init();
    write_sine_to_ram();

    while (true) {
        for (uint16_t i = 0; i < SINE_SAMPLES; i++) {
            uint8_t data[BYTES_PER_SAMPLE];
            spi_ram_read((uint16_t)(i * BYTES_PER_SAMPLE), data);
            spi_dac_write(data);
            sleep_ms(1);
        }
    }
}
