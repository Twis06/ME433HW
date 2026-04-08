#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"



int main()
{
    stdio_init_all();

    int servo_pin = 16;
    gpio_set_function(servo_pin, GPIO_FUNC_PWM);

    uint slice_num = pwm_gpio_to_slice_num(servo_pin); // find the PWM slice of the GPIO pin
    pwm_set_clkdiv(slice_num, 125); // devides clock, 125MHZ divided by 125 to 1MHz
    pwm_set_wrap(slice_num, 20000); // in one pwm warp, 20000/1M = 0.02sec -> 50hz (servo control frequency)

    pwm_set_enabled(slice_num, true);


    while (true) {
        sleep_ms(1000); // start monitor the serial output 
        pwm_set_gpio_level(servo_pin, 500);
        //
        // Calibration Code
        //
        // while (true) {  
        //     pwm_set_gpio_level(servo_pin, pulse);
        //     sleep_ms(20); 
        //     pulse += step;
        //     printf("current Pulse: %d \n", pulse);
        //     // if (pulse >= 2500 || pulse <= 300) {
        //     //     step = -step; 
        //     // } // max is 2900, min is 500
        // }

        // 
        // Cycle Movement 
        //
        int start_pulse = 600;
        int end_pulse = 2900;
        int step = 10;
        int pulse = start_pulse;
        while (true)
        {
            pwm_set_gpio_level(servo_pin, pulse);
            sleep_ms(10);
            pulse += step;

            if(pulse >= end_pulse || pulse <= start_pulse){
                step = -step;
            }
        }
    }
}



