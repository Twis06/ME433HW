#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define BAUD_RATE 115200

#define PICO_UART_TX 0  
#define PICO_UART_RX 1   

int main()
{
    stdio_init_all();
    sleep_ms(1000);

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(PICO_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICO_UART_RX, GPIO_FUNC_UART);

    printf("Pico UART bridge started\n");

    while (true) {
        // Computer>STM32
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID, (char)c);
        }
        while (uart_is_readable(UART_ID)) {
            putchar_raw(uart_getc(UART_ID));
        }
    }
}


