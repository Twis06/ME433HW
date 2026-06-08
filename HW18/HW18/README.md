# HW18 Force Feedback Joystick

This folder contains the Pico 2 W firmware for the two-axis force-feedback
joystick. The default `HW18` target builds `spring_feedback.c`.

## Wiring

All grounds are common: Pico GND, motor-driver GND, sensor GND, and motor
supply GND are tied together. The motors are powered from the motor supply
through the H-bridges, not from the Pico 3.3 V pin.

| Cable / Signal | Pico Connection | Connected Device |
|---|---|---|
| X encoder SDA | GP4, physical pin 6 | X AS5600 SDA |
| X encoder SCL | GP5, physical pin 7 | X AS5600 SCL |
| Y encoder SDA | GP6, physical pin 9 | Y AS5600 SDA |
| Y encoder SCL | GP7, physical pin 10 | Y AS5600 SCL |
| X motor input A | GP10, physical pin 14 | X H-bridge IN1 |
| X motor input B | GP11, physical pin 15 | X H-bridge IN2 |
| Y motor input A | GP12, physical pin 16 | Y H-bridge IN1 |
| Y motor input B | GP13, physical pin 17 | Y H-bridge IN2 |
| H-bridge enable/sleep | Pico 3V3 | Driver EN / SLEEP pulled high |
| Encoder power | Pico 3V3 and GND | AS5600 VCC and GND |
| Motor power | External motor supply | H-bridge VM / motor power input |

## Build

```sh
cmake --build build --target HW18
```

Flash `build/HW18.uf2` with the Pico extension run button or by copying the UF2
to the Pico bootloader drive.

## Firmware

`spring_feedback.c` keeps the joystick near the calibrated center using AS5600
encoder feedback. The controller uses signed encoder position, filtered encoder
velocity, a small breakaway drive floor, and adaptive stall boost. INA219 current
sensors are not used in this build.

Serial commands over USB:

| Command | Action |
|---|---|
| `z` | Set current encoder positions as zero and enable spring |
| `s` | Disable spring and stop both motors |
| `g` / `c` | Enable spring feedback |
| `+` / `-` | Increase or decrease spring strength |
| `x` / `y` | Flip X/Y motor direction if an axis pushes away from center |

Status lines print raw encoder counts, signed position, filtered velocity,
motor command, near-center assist, speed assist, adaptive boost, motor direction
flags, and encoder miss counts.
