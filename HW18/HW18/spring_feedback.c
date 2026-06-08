#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

#define X_I2C_PORT i2c0
#define X_I2C_SDA 4
#define X_I2C_SCL 5

#define Y_I2C_PORT i2c1
#define Y_I2C_SDA 6
#define Y_I2C_SCL 7

#define X_MOTOR_PWM_A 10
#define X_MOTOR_PWM_B 11
#define Y_MOTOR_PWM_A 12
#define Y_MOTOR_PWM_B 13

#define AS5600_ADDR 0x36
#define AS5600_ANGLE_REG 0x0E
#define AS5600_COUNTS_PER_REV 4096

// From calibration/ui_calibration.json.
#define X_ENCODER_ZERO_RAW 375
#define Y_ENCODER_ZERO_RAW 4009
#define X_ENCODER_SIGN -1
#define Y_ENCODER_SIGN 1
#define X_HARD_MIN_COUNTS -540
#define X_HARD_MAX_COUNTS 472
#define Y_HARD_MIN_COUNTS -482
#define Y_HARD_MAX_COUNTS 1289

#define CONTROL_DT_MS 5
#define PRINT_DT_MS 250
#define PWM_WRAP 1000
#define PWM_CLKDIV 1.0f

// Symmetric usable range around center. Y has a large positive measured range,
// but the useful spring feel should be similar in both directions.
#define X_MOTOR_INVERT true
#define Y_MOTOR_INVERT false

#define X_SPRING_FULL_COUNTS 430.0f
#define Y_SPRING_FULL_COUNTS 430.0f
#define CENTER_KP_PWM_PER_COUNT 0.90f
#define CENTER_CUBIC_PWM 90.0f
#define CENTER_KD_PWM_PER_COUNT_PER_S 0.12f
#define VELOCITY_FILTER_ALPHA 0.35f
#define CENTER_DEADBAND_COUNTS 3
#define CENTER_NEAR_ASSIST_PWM 45.0f
#define CENTER_NEAR_ASSIST_RISE_COUNTS 8.0f
#define CENTER_NEAR_ASSIST_FADE_COUNTS 110.0f
#define RETURN_SPEED_BASE_COUNTS_S 220.0f
#define RETURN_SPEED_GAIN_COUNTS_S_PER_COUNT 2.5f
#define RETURN_SPEED_MAX_COUNTS_S 1000.0f
#define RETURN_SPEED_KP_PWM_PER_COUNT_S 0.14f
#define RETURN_SPEED_ENABLE_COUNTS_S 80.0f
#define RETURN_SPEED_ASSIST_RISE_COUNTS 8.0f
#define RETURN_SPEED_ASSIST_MAX_PWM 160.0f

#define MOTOR_MAX_PWM 700
#define MOTOR_BREAKAWAY_PWM 120
#define BREAKAWAY_RAMP_COUNTS 85.0f
#define ADAPTIVE_STALL_MIN_COUNTS 16
#define ADAPTIVE_STALL_DELTA_COUNTS 1
#define ADAPTIVE_STALL_TICKS 3
#define ADAPTIVE_BOOST_STEP_PWM 6
#define ADAPTIVE_BOOST_DECAY_PWM 16
#define ADAPTIVE_BOOST_MAX_PWM 240
#define MOTOR_COMMAND_SLEW_PWM 70

#define SAFETY_MARGIN_COUNTS 50
#define ENCODER_STALE_LIMIT_MS 120
typedef struct {
    const char *name;
    i2c_inst_t *i2c;
    uint sda_pin;
    uint scl_pin;
    uint16_t zero;
    int32_t sign;
    uint16_t raw;
    int32_t position;
    int32_t last_raw;
    bool have_last_raw;
    bool have_position;
    uint32_t consecutive_misses;
    absolute_time_t last_ok_time;
} axis_encoder_t;

typedef struct {
    const char *name;
    uint pwm_a_pin;
    uint pwm_b_pin;
    bool invert;
    int32_t last_position;
    bool have_last_position;
    float filtered_velocity_counts_s;
    int32_t last_command;
    int32_t near_center_assist_pwm;
    int32_t speed_assist_pwm;
    int32_t adaptive_boost_pwm;
    uint32_t stall_ticks;
} motor_axis_t;

static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float clamp_f32(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float abs_f32(float value)
{
    return value < 0.0f ? -value : value;
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static bool as5600_read_raw(axis_encoder_t *axis, uint16_t *raw)
{
    uint8_t reg = AS5600_ANGLE_REG;
    uint8_t data[2];

    if (i2c_write_blocking(axis->i2c, AS5600_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    if (i2c_read_blocking(axis->i2c, AS5600_ADDR, data, 2, false) != 2) {
        return false;
    }

    *raw = ((data[0] << 8) | data[1]) & 0x0FFF;
    return true;
}

static int32_t zeroed_count(const axis_encoder_t *axis, uint16_t raw)
{
    int32_t count = (int32_t) raw - axis->zero;

    if (count > AS5600_COUNTS_PER_REV / 2) {
        count -= AS5600_COUNTS_PER_REV;
    } else if (count < -AS5600_COUNTS_PER_REV / 2) {
        count += AS5600_COUNTS_PER_REV;
    }

    return axis->sign * count;
}

static bool read_axis_position(axis_encoder_t *axis)
{
    uint16_t raw = 0;

    if (!as5600_read_raw(axis, &raw)) {
        if (axis->consecutive_misses < UINT32_MAX) {
            axis->consecutive_misses++;
        }
        return axis->have_position;
    }

    axis->last_raw = raw;
    axis->have_last_raw = true;
    axis->raw = raw;
    axis->position = zeroed_count(axis, raw);
    axis->have_position = true;
    axis->consecutive_misses = 0;
    axis->last_ok_time = get_absolute_time();
    return true;
}

static bool axis_position_fresh(const axis_encoder_t *axis, absolute_time_t now)
{
    if (!axis->have_position) {
        return false;
    }

    return absolute_time_diff_us(axis->last_ok_time, now) <= ENCODER_STALE_LIMIT_MS * 1000;
}

static void init_encoder_bus(axis_encoder_t *axis)
{
    i2c_init(axis->i2c, 400 * 1000);
    gpio_set_function(axis->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(axis->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(axis->sda_pin);
    gpio_pull_up(axis->scl_pin);
}

static void init_motor_axis(motor_axis_t *motor)
{
    gpio_set_function(motor->pwm_a_pin, GPIO_FUNC_PWM);
    gpio_set_function(motor->pwm_b_pin, GPIO_FUNC_PWM);

    uint slice_a = pwm_gpio_to_slice_num(motor->pwm_a_pin);
    uint slice_b = pwm_gpio_to_slice_num(motor->pwm_b_pin);

    pwm_set_wrap(slice_a, PWM_WRAP);
    pwm_set_wrap(slice_b, PWM_WRAP);
    pwm_set_clkdiv(slice_a, PWM_CLKDIV);
    pwm_set_clkdiv(slice_b, PWM_CLKDIV);
    pwm_set_gpio_level(motor->pwm_a_pin, 0);
    pwm_set_gpio_level(motor->pwm_b_pin, 0);
    pwm_set_enabled(slice_a, true);
    pwm_set_enabled(slice_b, true);
}

static void drive_motor_axis(const motor_axis_t *motor, int32_t command)
{
    command = clamp_i32(command, -MOTOR_MAX_PWM, MOTOR_MAX_PWM);

    if (motor->invert) {
        command = -command;
    }

    if (command > 0) {
        pwm_set_gpio_level(motor->pwm_a_pin, (uint16_t) command);
        pwm_set_gpio_level(motor->pwm_b_pin, 0);
    } else if (command < 0) {
        pwm_set_gpio_level(motor->pwm_a_pin, 0);
        pwm_set_gpio_level(motor->pwm_b_pin, (uint16_t) -command);
    } else {
        pwm_set_gpio_level(motor->pwm_a_pin, 0);
        pwm_set_gpio_level(motor->pwm_b_pin, 0);
    }
}

static void stop_motors(const motor_axis_t *x_motor, const motor_axis_t *y_motor)
{
    drive_motor_axis(x_motor, 0);
    drive_motor_axis(y_motor, 0);
}

static void reset_motor_feedback_state(motor_axis_t *motor)
{
    motor->last_position = 0;
    motor->have_last_position = false;
    motor->filtered_velocity_counts_s = 0.0f;
    motor->last_command = 0;
    motor->near_center_assist_pwm = 0;
    motor->speed_assist_pwm = 0;
    motor->adaptive_boost_pwm = 0;
    motor->stall_ticks = 0;
}

static bool inside_safety_window(int32_t x_position, int32_t y_position)
{
    return x_position >= X_HARD_MIN_COUNTS - SAFETY_MARGIN_COUNTS &&
           x_position <= X_HARD_MAX_COUNTS + SAFETY_MARGIN_COUNTS &&
           y_position >= Y_HARD_MIN_COUNTS - SAFETY_MARGIN_COUNTS &&
           y_position <= Y_HARD_MAX_COUNTS + SAFETY_MARGIN_COUNTS;
}

static int32_t apply_breakaway_pwm(int32_t command, int32_t position)
{
    if (command == 0) {
        return 0;
    }

    float active_counts = abs_f32((float) position) - (float) CENTER_DEADBAND_COUNTS;
    float ramp = clamp_f32(active_counts / BREAKAWAY_RAMP_COUNTS, 0.0f, 1.0f);
    int32_t minimum = (int32_t) ((float) MOTOR_BREAKAWAY_PWM * ramp);
    int32_t magnitude = command < 0 ? -command : command;

    if (magnitude < minimum) {
        magnitude = minimum;
    }

    return command < 0 ? -magnitude : magnitude;
}

static int32_t apply_adaptive_boost(motor_axis_t *motor, int32_t command, int32_t position)
{
    if (command == 0 || abs_i32(position) <= ADAPTIVE_STALL_MIN_COUNTS) {
        motor->stall_ticks = 0;
        motor->adaptive_boost_pwm = clamp_i32(motor->adaptive_boost_pwm - (ADAPTIVE_BOOST_DECAY_PWM * 2),
                                              0,
                                              ADAPTIVE_BOOST_MAX_PWM);
        return command;
    }

    if (motor->have_last_position) {
        int32_t position_delta = position - motor->last_position;
        int32_t previous_abs = abs_i32(motor->last_position);
        int32_t current_abs = abs_i32(position);
        int32_t recovery_counts = previous_abs - current_abs;

        if (recovery_counts > ADAPTIVE_STALL_DELTA_COUNTS) {
            motor->stall_ticks = 0;
            motor->adaptive_boost_pwm = clamp_i32(motor->adaptive_boost_pwm - ADAPTIVE_BOOST_DECAY_PWM,
                                                  0,
                                                  ADAPTIVE_BOOST_MAX_PWM);
        } else if (abs_i32(position_delta) <= ADAPTIVE_STALL_DELTA_COUNTS) {
            if (motor->stall_ticks < UINT32_MAX) {
                motor->stall_ticks++;
            }
            if (motor->stall_ticks >= ADAPTIVE_STALL_TICKS) {
                motor->adaptive_boost_pwm = clamp_i32(motor->adaptive_boost_pwm + ADAPTIVE_BOOST_STEP_PWM,
                                                      0,
                                                      ADAPTIVE_BOOST_MAX_PWM);
            }
        } else {
            motor->stall_ticks = 0;
            motor->adaptive_boost_pwm = clamp_i32(motor->adaptive_boost_pwm - (ADAPTIVE_BOOST_DECAY_PWM / 2),
                                                  0,
                                                  ADAPTIVE_BOOST_MAX_PWM);
        }
    }

    if (motor->adaptive_boost_pwm == 0) {
        return command;
    }

    int32_t magnitude = abs_i32(command) + motor->adaptive_boost_pwm;
    magnitude = clamp_i32(magnitude, 0, MOTOR_MAX_PWM);
    return command < 0 ? -magnitude : magnitude;
}

static int32_t apply_command_slew(motor_axis_t *motor, int32_t target_command)
{
    int32_t delta = target_command - motor->last_command;

    delta = clamp_i32(delta, -MOTOR_COMMAND_SLEW_PWM, MOTOR_COMMAND_SLEW_PWM);
    motor->last_command = clamp_i32(motor->last_command + delta, -MOTOR_MAX_PWM, MOTOR_MAX_PWM);
    return motor->last_command;
}

static float near_center_assist_force(int32_t position)
{
    float active_counts = abs_f32((float) position) - (float) CENTER_DEADBAND_COUNTS;

    if (active_counts <= 0.0f || active_counts >= CENTER_NEAR_ASSIST_FADE_COUNTS) {
        return 0.0f;
    }

    float rise = clamp_f32(active_counts / CENTER_NEAR_ASSIST_RISE_COUNTS, 0.0f, 1.0f);
    float fade = 1.0f - clamp_f32(active_counts / CENTER_NEAR_ASSIST_FADE_COUNTS, 0.0f, 1.0f);
    float assist = CENTER_NEAR_ASSIST_PWM * rise * fade;

    return position < 0 ? -assist : assist;
}

static float return_speed_assist_force(motor_axis_t *motor, int32_t position)
{
    float active_counts = abs_f32((float) position) - (float) CENTER_DEADBAND_COUNTS;

    if (active_counts <= 0.0f) {
        return 0.0f;
    }

    float desired_recovery_speed =
        RETURN_SPEED_BASE_COUNTS_S + (active_counts * RETURN_SPEED_GAIN_COUNTS_S_PER_COUNT);
    desired_recovery_speed = clamp_f32(desired_recovery_speed, 0.0f, RETURN_SPEED_MAX_COUNTS_S);

    float actual_recovery_speed = position > 0 ? -motor->filtered_velocity_counts_s : motor->filtered_velocity_counts_s;
    if (actual_recovery_speed < RETURN_SPEED_ENABLE_COUNTS_S) {
        return 0.0f;
    }

    float speed_error = desired_recovery_speed - actual_recovery_speed;

    if (speed_error <= 0.0f) {
        return 0.0f;
    }

    float rise = clamp_f32(active_counts / RETURN_SPEED_ASSIST_RISE_COUNTS, 0.0f, 1.0f);
    float assist = speed_error * RETURN_SPEED_KP_PWM_PER_COUNT_S * rise;
    assist = clamp_f32(assist, 0.0f, RETURN_SPEED_ASSIST_MAX_PWM);

    return position < 0 ? -assist : assist;
}

static int32_t center_hold_command(motor_axis_t *motor,
                                   int32_t position,
                                   float full_range_counts,
                                   float strength_scale)
{
    float raw_velocity_counts_s = 0.0f;

    if (motor->have_last_position) {
        raw_velocity_counts_s = (float) (position - motor->last_position) * (1000.0f / (float) CONTROL_DT_MS);
        motor->filtered_velocity_counts_s =
            (VELOCITY_FILTER_ALPHA * raw_velocity_counts_s) +
            ((1.0f - VELOCITY_FILTER_ALPHA) * motor->filtered_velocity_counts_s);
    } else {
        motor->filtered_velocity_counts_s = 0.0f;
    }

    if (abs_f32((float) position) <= CENTER_DEADBAND_COUNTS) {
        motor->last_position = position;
        motor->have_last_position = true;
        motor->stall_ticks = 0;
        motor->adaptive_boost_pwm = clamp_i32(motor->adaptive_boost_pwm - (ADAPTIVE_BOOST_DECAY_PWM * 2),
                                              0,
                                              ADAPTIVE_BOOST_MAX_PWM);
        motor->last_command = 0;
        motor->near_center_assist_pwm = 0;
        motor->speed_assist_pwm = 0;
        return 0;
    }

    float normalized = clamp_f32((float) position / full_range_counts, -1.25f, 1.25f);
    float assist_force = near_center_assist_force(position);
    float speed_assist_force = return_speed_assist_force(motor, position);
    float spring_force = (CENTER_KP_PWM_PER_COUNT * (float) position) +
                         (CENTER_CUBIC_PWM * normalized * normalized * normalized) +
                         assist_force +
                         speed_assist_force;
    float damping_force = CENTER_KD_PWM_PER_COUNT_PER_S * motor->filtered_velocity_counts_s;
    motor->near_center_assist_pwm = (int32_t) assist_force;
    motor->speed_assist_pwm = (int32_t) speed_assist_force;

    int32_t command = (int32_t) (strength_scale * -(spring_force + damping_force));
    command = apply_breakaway_pwm(command, position);
    command = apply_adaptive_boost(motor, command, position);
    command = clamp_i32(command, -MOTOR_MAX_PWM, MOTOR_MAX_PWM);
    command = apply_command_slew(motor, command);

    motor->last_position = position;
    motor->have_last_position = true;
    return command;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    axis_encoder_t x_axis = {
        .name = "X",
        .i2c = X_I2C_PORT,
        .sda_pin = X_I2C_SDA,
        .scl_pin = X_I2C_SCL,
        .zero = X_ENCODER_ZERO_RAW,
        .sign = X_ENCODER_SIGN,
    };
    axis_encoder_t y_axis = {
        .name = "Y",
        .i2c = Y_I2C_PORT,
        .sda_pin = Y_I2C_SDA,
        .scl_pin = Y_I2C_SCL,
        .zero = Y_ENCODER_ZERO_RAW,
        .sign = Y_ENCODER_SIGN,
    };
    motor_axis_t x_motor = {
        .name = "X",
        .pwm_a_pin = X_MOTOR_PWM_A,
        .pwm_b_pin = X_MOTOR_PWM_B,
        .invert = X_MOTOR_INVERT,
    };
    motor_axis_t y_motor = {
        .name = "Y",
        .pwm_a_pin = Y_MOTOR_PWM_A,
        .pwm_b_pin = Y_MOTOR_PWM_B,
        .invert = Y_MOTOR_INVERT,
    };
    init_encoder_bus(&x_axis);
    init_encoder_bus(&y_axis);
    init_motor_axis(&x_motor);
    init_motor_axis(&y_motor);
    stop_motors(&x_motor, &y_motor);

    printf("\nHW18 spring feedback\n");
    printf("Center is calibrated zero. Clockwise is +, counter-clockwise is -.\n");
    printf("Center hold: encoder Kp %.2f PWM/count + Kd %.2f PWM/(count/s) + cubic %.0f PWM + near assist %.0f PWM\n",
           CENTER_KP_PWM_PER_COUNT,
           CENTER_KD_PWM_PER_COUNT_PER_S,
           CENTER_CUBIC_PWM,
           CENTER_NEAR_ASSIST_PWM);
    printf("Return speed target: %.0f + %.1f*counts, capped at %.0f counts/s, active above %.0f counts/s\n",
           RETURN_SPEED_BASE_COUNTS_S,
           RETURN_SPEED_GAIN_COUNTS_S_PER_COUNT,
           RETURN_SPEED_MAX_COUNTS_S,
           RETURN_SPEED_ENABLE_COUNTS_S);
    printf("Motor invert: X=%d Y=%d\n", x_motor.invert, y_motor.invert);
    printf("Commands: z=set zero, s=disable, g=enable, +=stronger, -=weaker, x/y=flip motor.\n");
    printf("Encoder position + velocity loop. Current sensors are not used for feedback in this build. Max PWM %d.\n\n",
           MOTOR_MAX_PWM);
    printf("Adaptive boost: if encoder position stalls away from zero, add up to %d PWM.\n\n",
           ADAPTIVE_BOOST_MAX_PWM);

    printf("Spring feedback enabled.\n\n");

    bool active = true;
    bool tripped = false;
    bool x_limited = false;
    bool y_limited = false;
    int32_t x_command = 0;
    int32_t y_command = 0;
    float strength_scale = 1.00f;
    absolute_time_t last_control_time = get_absolute_time();
    absolute_time_t last_print_time = get_absolute_time();

    while (true) {
        read_axis_position(&x_axis);
        read_axis_position(&y_axis);
        absolute_time_t now = get_absolute_time();

        int ch = getchar_timeout_us(0);
        if (ch == 's' || ch == 'S') {
            active = false;
            tripped = false;
            stop_motors(&x_motor, &y_motor);
            reset_motor_feedback_state(&x_motor);
            reset_motor_feedback_state(&y_motor);
            printf("\nspring disabled\n");
        } else if (ch == 'g' || ch == 'G' || ch == 'c' || ch == 'C') {
            active = true;
            tripped = false;
            reset_motor_feedback_state(&x_motor);
            reset_motor_feedback_state(&y_motor);
            printf("\nspring enabled\n");
        } else if (ch == 'z' || ch == 'Z') {
            if (x_axis.have_position) {
                x_axis.zero = x_axis.raw;
                x_axis.position = 0;
            }
            if (y_axis.have_position) {
                y_axis.zero = y_axis.raw;
                y_axis.position = 0;
            }
            reset_motor_feedback_state(&x_motor);
            reset_motor_feedback_state(&y_motor);
            active = true;
            tripped = false;
            stop_motors(&x_motor, &y_motor);
            printf("\nzero set: X=%u Y=%u; spring enabled\n", x_axis.zero, y_axis.zero);
        } else if (ch == '+') {
            strength_scale = clamp_f32(strength_scale + 0.10f, 0.30f, 3.00f);
            printf("\nspring strength=%.2f\n", strength_scale);
        } else if (ch == '-') {
            strength_scale = clamp_f32(strength_scale - 0.10f, 0.30f, 3.00f);
            printf("\nspring strength=%.2f\n", strength_scale);
        } else if (ch == 'x' || ch == 'X') {
            x_motor.invert = !x_motor.invert;
            reset_motor_feedback_state(&x_motor);
            stop_motors(&x_motor, &y_motor);
            printf("\nX motor invert=%d\n", x_motor.invert);
        } else if (ch == 'y' || ch == 'Y') {
            y_motor.invert = !y_motor.invert;
            reset_motor_feedback_state(&y_motor);
            stop_motors(&x_motor, &y_motor);
            printf("\nY motor invert=%d\n", y_motor.invert);
        }

        int32_t x_position = x_axis.have_position ? x_axis.position : 0;
        int32_t y_position = y_axis.have_position ? y_axis.position : 0;
        bool x_fresh = axis_position_fresh(&x_axis, now);
        bool y_fresh = axis_position_fresh(&y_axis, now);
        float control_elapsed_s = (float) absolute_time_diff_us(last_control_time, now) / 1000000.0f;
        if (control_elapsed_s >= (float) CONTROL_DT_MS / 1000.0f) {
            last_control_time = now;

            if (active && !tripped && x_fresh && y_fresh && inside_safety_window(x_position, y_position)) {
                x_limited = false;
                y_limited = false;
                x_command = center_hold_command(&x_motor,
                                                x_position,
                                                X_SPRING_FULL_COUNTS,
                                                strength_scale);
                y_command = center_hold_command(&y_motor,
                                                y_position,
                                                Y_SPRING_FULL_COUNTS,
                                                strength_scale);
                drive_motor_axis(&x_motor, x_command);
                drive_motor_axis(&y_motor, y_command);
            } else {
                if (active && !tripped) {
                    printf("\nsafety stop: fresh X=%d Y=%d miss X=%lu Y=%lu pos X=%ld Y=%ld\n",
                           x_fresh,
                           y_fresh,
                           (unsigned long) x_axis.consecutive_misses,
                           (unsigned long) y_axis.consecutive_misses,
                           (long) x_position,
                           (long) y_position);
                }
                active = false;
                x_command = 0;
                y_command = 0;
                x_limited = false;
                y_limited = false;
                reset_motor_feedback_state(&x_motor);
                reset_motor_feedback_state(&y_motor);
                stop_motors(&x_motor, &y_motor);
            }
        }

        if (absolute_time_diff_us(last_print_time, now) >= PRINT_DT_MS * 1000) {
            last_print_time = now;
            printf("spring active=%d trip=%d strength=%.2f raw X=%4u Y=%4u pos X=%5ld Y=%5ld vel X=%6ld Y=%6ld cmd X=%4ld%s Y=%4ld%s assist X=%4ld Y=%4ld speed X=%4ld Y=%4ld boost X=%3ld Y=%3ld stall X=%lu Y=%lu inv X=%d Y=%d miss X=%lu Y=%lu | ",
                   active,
                   tripped,
                   strength_scale,
                   x_axis.raw,
                   y_axis.raw,
                   (long) x_position,
                   (long) y_position,
                   (long) x_motor.filtered_velocity_counts_s,
                   (long) y_motor.filtered_velocity_counts_s,
                   (long) x_command,
                   x_limited ? "*" : "",
                   (long) y_command,
                   y_limited ? "*" : "",
                   (long) x_motor.near_center_assist_pwm,
                   (long) y_motor.near_center_assist_pwm,
                   (long) x_motor.speed_assist_pwm,
                   (long) y_motor.speed_assist_pwm,
                   (long) x_motor.adaptive_boost_pwm,
                   (long) y_motor.adaptive_boost_pwm,
                   (unsigned long) x_motor.stall_ticks,
                   (unsigned long) y_motor.stall_ticks,
                   x_motor.invert,
                   y_motor.invert,
                   (unsigned long) x_axis.consecutive_misses,
                   (unsigned long) y_axis.consecutive_misses);
            printf("\n");
            fflush(stdout);
        }

        sleep_ms(1);
    }
}
