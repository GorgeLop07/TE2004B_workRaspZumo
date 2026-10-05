#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "motors.h"
#include "encoder.h"

#define COUNTS_PER_METER  7375.5
#define FORWARD_COUNTS_CORRECTION   516.285
#define BASE_SPEED        2200
#define KP                4
#define TURN_SPEED        1500

#define COUNTS_PER_90_DEGREES  598

/* Counts per degree of rotation, derived from the 90-degree calibration. */
#define COUNTS_PER_DEGREE      (COUNTS_PER_90_DEGREES / 90.0)
/* Extra counts for a small angular correction (e.g. finishing at 87 deg
 * instead of 90 deg means 3 deg are missing). */
#define ANGLE_CORRECTION_DEG     10
#define ANGLE_CORRECTION_COUNTS  ((int32_t)(COUNTS_PER_DEGREE * ANGLE_CORRECTION_DEG))

/* Forward motion produces negative raw counts on the Zumo, so the sign is
 * inverted here. After this, forward = positive on both sides. */
static void read_counts(int32_t *left, int32_t *right) {
    int32_t raw_left, raw_right;
    encoder_get_counts(&raw_left, &raw_right);
    *left = -raw_left;
    *right = -raw_right;
}

void drive_straight_counts(int32_t target_counts) {
    encoder_reset();
    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t distance = (left + right) / 2;
        if (distance >= target_counts) {
            break;
        }
        int32_t error = left - right;
        int32_t correction = KP * error;
        int32_t left_speed = BASE_SPEED - correction;
        int32_t right_speed = BASE_SPEED + correction;
        /* Limit correction */
        if (left_speed > 3000)
            left_speed = 3000;
        if (left_speed < 1000)
            left_speed = 1000;
        if (right_speed > 3000)
            right_speed = 3000;
        if (right_speed < 1000)
            right_speed = 1000;
        motors_set_speeds(left_speed, right_speed);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

void turn_right_90(int32_t target_counts) {
    encoder_reset();
    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t rotation = (abs(left) + abs(right)) / 2;
        if (rotation >= target_counts) {
            break;
        }
        motors_set_speeds(TURN_SPEED, -TURN_SPEED);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

void turn_right_90_precise(int32_t target) {
    encoder_reset();

    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        left = abs(left);
        right = abs(right);
        int32_t count = (left + right) / 2;
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t remaining = target - count;
        if (remaining <= 0)
            break;
        int speed;
        if (remaining > 300)
            speed = 1800;
        else if (remaining > 100)
            speed = 1200;
        else
            speed = 700;
        motors_set_speeds(speed, -speed);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

void drive_one_meter(int32_t target) {
    encoder_reset();
    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t average = (left + right) / 2;
        if (average >= target)
            break;
        int32_t remaining = target - average;
        int base_speed;
        if (remaining > 1000)
            base_speed = 2200;
        else if (remaining > 400)
            base_speed = 1500;
        else
            base_speed = 800;
        int32_t error = left - right;
        int32_t correction = KP * error;
        motors_set_speeds(base_speed - correction, base_speed + correction);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

void move_forward_correction(int32_t counts) {
    encoder_reset();
    while (true)
    {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t average = (left + right) / 2;
        if (average >= counts)
            break;
        motors_set_speeds(700, 700);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
}

void move_backward_correction(int32_t counts) {
    encoder_reset();
    while (true)
    {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t average = (left + right) / 2;
        if (-average >= counts)
            break;
        motors_set_speeds(-700, -700);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
}

/* Small turn to the right (clockwise) at low speed. */
void turn_right_correction(int32_t counts) {
    encoder_reset();
    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t rotation = (abs(left) + abs(right)) / 2;
        if (rotation >= counts)
            break;
        motors_set_speeds(1000, -1000);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

/* Small turn to the left (counterclockwise), for when the robot overshoots. */
void turn_left_correction(int32_t counts) {
    encoder_reset();
    while (true) {
        int32_t left, right;
        read_counts(&left, &right);
        printf("L = %ld R = %ld\n", (long)left, (long)right);
        int32_t rotation = (abs(left) + abs(right)) / 2;
        if (rotation >= counts)
            break;
        motors_set_speeds(-800, 800);
        sleep_ms(5);
    }
    motors_set_speeds(0, 0);
    sleep_ms(300);
}

int main(void) {
    stdio_init_all();
    motors_init();
    encoder_init();
    encoder_reset();
    sleep_ms(3000);

    for (int side = 0; side < 4; side++) {
        printf("Side %d\n", side + 1);

        /* Travel one meter. */
        drive_one_meter(COUNTS_PER_METER);
        move_forward_correction(FORWARD_COUNTS_CORRECTION);
        printf("Straight completed\n");

        /* Rotate 90 degrees clockwise. */
        turn_right_90(COUNTS_PER_90_DEGREES);
        turn_right_correction(ANGLE_CORRECTION_COUNTS);
        printf("Turn completed\n");
    }
    motors_set_speeds(0, 0);
    printf("Square completed.\n");

    while (true) {
        tight_loop_contents();
    }
}
