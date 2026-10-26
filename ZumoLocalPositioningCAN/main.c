#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "motors.h"
#include "button.h"
#include "mcp2515.h"
#include "lp_can.h"

#define TARGET_DISTANCE_MM  500
#define STOP_EARLY_MM       0      // compensates overshoot caused by camera/CAN latency
#define SLOW_ZONE_MM        100
#define FAST_SPEED          1500
#define SLOW_SPEED          700
#define HEADING_KP          0.0f   // speed units per 0.1 deg of heading error, 0 disables
#define CAN_TIMEOUT_MS      300

typedef enum {
    STATE_WAIT_START,
    STATE_MOVING,
    STATE_DONE
} state_t;

static const char *state_name(state_t s) {
    switch (s) {
        case STATE_WAIT_START: return "WAIT_START";
        case STATE_MOVING:     return "MOVING";
        default:               return "DONE";
    }
}

static int32_t wrap_ddeg(int32_t a) {
    while (a > 1800) a -= 3600;
    while (a < -1800) a += 3600;
    return a;
}

static bool lp_usable(const lp_data_t *lp) {
    return (lp->status & (LP_STATUS_VALID | LP_STATUS_FRESH)) == (LP_STATUS_VALID | LP_STATUS_FRESH);
}

int main(void) {
    stdio_init_all();
    motors_init();
    motors_set_speeds(0, 0);
    sleep_ms(3000); // give the USB serial connection time to enumerate

    printf("\n--- Zumo Local Positioning over CAN ---\n");
    mcp2515_init();
    uint8_t canstat = mcp2515_read_register(MCP2515_CANSTAT);
    printf("MCP2515 CANSTAT=0x%02X after reset (0x80 = SPI OK)\n", canstat);
    if (!mcp2515_set_bitrate_500kbps_8mhz()) {
        printf("MCP2515 config failed. Check wiring.\n");
    }
    if (mcp2515_set_mode(MCP2515_MODE_NORMAL)) {
        printf("MCP2515 in NORMAL mode\n");
    } else {
        printf("MCP2515 failed to enter NORMAL mode\n");
    }

    button button_a = BUTTON_INIT(button_a_is_pressed);
    state_t state = STATE_WAIT_START;
    lp_data_t lp = {0};
    lp_data_t start = {0};
    bool have_lp = false;
    absolute_time_t last_rx = get_absolute_time();
    absolute_time_t next_status = make_timeout_time_ms(500);
    int32_t left = 0, right = 0;

    printf("Press A to start. Target: %d mm along y\n", TARGET_DISTANCE_MM);

    while (true) {
        bool new_frame = false;
        can_frame_t rx;
        if (mcp2515_read_message(&rx)) {
            if (lp_unpack(&rx, &lp)) {
                have_lp = true;
                new_frame = true;
                last_rx = get_absolute_time();
            } else {
                printf("RX  ID=0x%03X  DLC=%d (ignored)\n", (unsigned)rx.id, rx.dlc);
            }
        }

        bool can_alive = have_lp &&
            absolute_time_diff_us(last_rx, get_absolute_time()) < CAN_TIMEOUT_MS * 1000;

        if (state != STATE_MOVING && button_check(&button_a) == 1) {
            if (can_alive && lp_usable(&lp)) {
                start = lp;
                state = STATE_MOVING;
                printf("START x=%d mm y=%d mm angle=%.1f deg -> target y=%d +/- %d mm\n",
                       start.x_mm, start.y_mm, start.angle_ddeg / 10.0f,
                       start.y_mm, TARGET_DISTANCE_MM);
            } else {
                printf("Cannot start: no fresh LP data over CAN\n");
            }
        }

        if (state == STATE_MOVING) {
            if (!can_alive || !lp_usable(&lp)) {
                if (left != 0 || right != 0) {
                    printf("LP data lost, stopping until it returns\n");
                }
                left = right = 0;
                motors_set_speeds(0, 0);
            } else if (new_frame) {
                /* Distance along y regardless of the direction the tractor faces. */
                int32_t progress = abs(lp.y_mm - start.y_mm);
                if (progress >= TARGET_DISTANCE_MM - STOP_EARLY_MM) {
                    left = right = 0;
                    motors_set_speeds(0, 0);
                    state = STATE_DONE;
                } else {
                    int32_t speed = (progress >= TARGET_DISTANCE_MM - SLOW_ZONE_MM) ? SLOW_SPEED : FAST_SPEED;
                    int32_t heading_error = wrap_ddeg(lp.angle_ddeg - start.angle_ddeg);
                    int32_t correction = (int32_t)(HEADING_KP * heading_error);
                    left = speed + correction;
                    right = speed - correction;
                    motors_set_speeds(left, right);
                }

                printf("RX  ID=0x%03X  DLC=%d  Data=", (unsigned)rx.id, rx.dlc);
                for (int i = 0; i < rx.dlc; i++) printf("%02X ", rx.data[i]);
                printf("| x=%d y=%d angle=%.1f seq=%u | dy=%ld/%d mm | %s L=%ld R=%ld\n",
                       lp.x_mm, lp.y_mm, lp.angle_ddeg / 10.0f, lp.seq,
                       (long)progress, TARGET_DISTANCE_MM, state_name(state),
                       (long)left, (long)right);

                if (state == STATE_DONE) {
                    printf("DESTINATION REACHED x=%d mm y=%d mm angle=%.1f deg (dx=%d dy=%d mm)\n",
                           lp.x_mm, lp.y_mm, lp.angle_ddeg / 10.0f,
                           lp.x_mm - start.x_mm, lp.y_mm - start.y_mm);
                    printf("Press A to run again from the current position\n");
                }
            }
        } else if (time_reached(next_status)) {
            next_status = make_timeout_time_ms(500);
            if (can_alive) {
                printf("%s | x=%d mm y=%d mm angle=%.1f deg status=0x%02X seq=%u\n",
                       state_name(state), lp.x_mm, lp.y_mm, lp.angle_ddeg / 10.0f,
                       lp.status, lp.seq);
            } else {
                printf("%s | waiting for CAN ID 0x%03X\n", state_name(state), LP_CAN_ID);
            }
        }
    }
}
