#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "button.h"
#include "motors.h"
#include "mcp2515.h"
#include "lp_can.h"
#include "encoders.h"
#include "route_config.h"

#define PI_F 3.14159265358979323846f

typedef enum {
    CAN_MONITOR,
    RUNNING,
    PAUSED_DATA_LOSS,
    DONE
} route_state_t;

typedef struct { float x, y; } point_t;

static const char *state_name(route_state_t s)
{
    switch (s) {
        case CAN_MONITOR: return "CAN_MONITOR";
        case RUNNING: return "RUNNING";
        case PAUSED_DATA_LOSS: return "PAUSED_DATA_LOSS";
        default: return "DONE";
    }
}

static int32_t wrap_ddeg(int32_t angle)
{
    while (angle > 1800) angle -= 3600;
    while (angle < -1800) angle += 3600;
    return angle;
}

static float radians_to_ddeg(float radians)
{
    return radians * (1800.0f / PI_F);
}

static point_t waypoint_point(uint8_t id)
{
    point_t p = { route_waypoints[id].x_mm, route_waypoints[id].y_mm };
    return p;
}

static float distance_to(const lp_data_t *pose, point_t p)
{
    float dx = (float)pose->x_mm - p.x;
    float dy = (float)pose->y_mm - p.y;
    return sqrtf(dx * dx + dy * dy);
}

static bool pose_usable(const lp_data_t *pose)
{
    return (pose->status & (LP_STATUS_VALID | LP_STATUS_FRESH)) ==
           (LP_STATUS_VALID | LP_STATUS_FRESH);
}

static int clamp_speed(int value)
{
    if (value > ROUTE_MAX_SPEED) return ROUTE_MAX_SPEED;
    if (value < -ROUTE_MAX_SPEED) return -ROUTE_MAX_SPEED;
    return value;
}

static int32_t clamp_encoder_trim(float value)
{
    if (value > ROUTE_ENCODER_MAX_TRIM) return ROUTE_ENCODER_MAX_TRIM;
    if (value < -ROUTE_ENCODER_MAX_TRIM) return -ROUTE_ENCODER_MAX_TRIM;
    return (int32_t)lroundf(value);
}

static void update_encoder_trim(int32_t previous_left_command,
                                int32_t previous_right_command,
                                int32_t left_delta, int32_t right_delta,
                                int32_t elapsed_ms,
                                float *left_integral, float *right_integral,
                                int32_t *left_trim, int32_t *right_trim)
{
    if (elapsed_ms <= 0 || elapsed_ms > 500) return;

    const int32_t commands[2] = { previous_left_command, previous_right_command };
    const int32_t deltas[2] = { left_delta, right_delta };
    float *integrals[2] = { left_integral, right_integral };
    int32_t *trims[2] = { left_trim, right_trim };
    const float interval_scale = (float)elapsed_ms / 100.0f;

    for (unsigned i = 0; i < 2; ++i) {
        int32_t command = commands[i];
        if (command == 0) {
            *integrals[i] = 0.0f;
            *trims[i] = 0;
            continue;
        }

        int direction = command < 0 ? -1 : 1;
        float target_ticks = abs(command) * ROUTE_ENCODER_TICKS_PER_PWM_100MS * interval_scale;
        float measured_ticks = deltas[i] * direction;
        float error = target_ticks - measured_ticks;
        *integrals[i] += error;
        if (*integrals[i] > ROUTE_ENCODER_MAX_INTEGRAL)
            *integrals[i] = ROUTE_ENCODER_MAX_INTEGRAL;
        if (*integrals[i] < -ROUTE_ENCODER_MAX_INTEGRAL)
            *integrals[i] = -ROUTE_ENCODER_MAX_INTEGRAL;
        *trims[i] = clamp_encoder_trim(ROUTE_ENCODER_KP * error +
                                       ROUTE_ENCODER_KI * *integrals[i]);
    }
}

static int32_t apply_encoder_trim(int32_t command, int32_t trim)
{
    if (command == 0) return 0;
    int direction = command < 0 ? -1 : 1;
    int magnitude = abs(command) + trim;
    if (magnitude < 0) magnitude = 0;
    return clamp_speed(direction * magnitude);
}

static void print_can_frame(const char *direction, const can_frame_t *frame)
{
    printf("%s ID=0x%03X DLC=%u Data=", direction,
           (unsigned)frame->id, frame->dlc);
    for (uint8_t i = 0; i < frame->dlc && i < 8; ++i) {
        printf("%02X ", frame->data[i]);
    }
    printf("\n");
}

static bool send_can_diagnostic(uint8_t sequence)
{
    can_frame_t frame = { .id = 0x102, .dlc = 4,
                          .data = { 0x5A, 0x55, sequence, 0xA5 } };
    if (mcp2515_send_message(&frame)) {
        print_can_frame("TX", &frame);
        return true;
    } else {
        printf("TX failed ID=0x%03X (no CAN ACK or MCP2515 TX error)\n",
               (unsigned)frame.id);
        return false;
    }
}

int main(void)
{
    stdio_init_all();
    motors_init();
    motors_set_speeds(0, 0);
    sleep_ms(1500);

    printf("\nZumo 2040 ordered route follower\n");
    printf("Route calibrated: %s | %s\n",
           ROUTE_CALIBRATED ? "YES" : "NO", ROUTE_COORDINATE_NOTES);
    printf("Order: P14 -> P1 -> ... -> P14\n");
    if (!ROUTE_CALIBRATED) {
        printf("Motion disabled: survey all 14 waypoints and document camera coordinates first.\n");
    }

    mcp2515_init();
    uint8_t reset_canstat = mcp2515_read_register(MCP2515_CANSTAT);
    printf("MCP2515 CANSTAT after reset=0x%02X (configuration mode should read 0x80)\n",
           reset_canstat);
    if ((reset_canstat & MCP2515_MODE_MASK) != MCP2515_MODE_CONFIG) {
        printf("MCP2515 SPI/register check failed; inspect MISO, MOSI, SCK, CS and power.\n");
        while (true) { motors_set_speeds(0, 0); sleep_ms(100); }
    }
    if (!mcp2515_set_bitrate_500kbps_8mhz() ||
        !mcp2515_set_mode(MCP2515_MODE_NORMAL)) {
        printf("MCP2515 setup failed (CANSTAT=0x%02X CANCTRL=0x%02X); motors remain stopped.\n",
               mcp2515_read_register(MCP2515_CANSTAT),
               mcp2515_read_register(MCP2515_CANCTRL));
        while (true) { motors_set_speeds(0, 0); sleep_ms(100); }
    }

    bool encoders_ready = encoders_init();
    printf("Wheel encoders: %s (left GP12/13, right GP8/9; MCP2515 INT must be disconnected from GP12)\n",
           encoders_ready ? "ready" : "initialization failed");

    button button_b = BUTTON_INIT(button_b_is_pressed);
    button button_c = BUTTON_INIT(button_c_is_pressed);
    route_state_t state = CAN_MONITOR;
    lp_data_t pose = {0};
    bool have_pose = false, have_sequence = false;
    uint8_t last_sequence = 0;
    absolute_time_t last_can_rx = get_absolute_time();
    absolute_time_t last_sequence_progress = get_absolute_time();
    absolute_time_t last_report = get_absolute_time();
    uint8_t target_order_index = 1; /* route_order[0] is the P14 start point */
    int32_t left = 0, right = 0;
    uint8_t diagnostic_sequence = 0;
    int32_t encoder_left_count = 0, encoder_right_count = 0;
    int32_t encoder_last_left_count = 0, encoder_last_right_count = 0;
    int32_t encoder_left_window = 0, encoder_right_window = 0;
    int32_t encoder_trim_left = 0, encoder_trim_right = 0;
    float encoder_integral_left = 0.0f, encoder_integral_right = 0.0f;
    bool encoder_count_baseline = encoders_ready &&
        encoders_read(&encoder_last_left_count, &encoder_last_right_count);
    bool encoder_feedback_active = false;
    absolute_time_t last_encoder_feedback = get_absolute_time();

    printf("Live USB serial CAN monitor active: RX frames and decoded pose will print below.\n");
    printf("Coordinate navigation uses camera x/y/heading only; no line sensor calibration is used.\n");
    printf("Buttons or terminal keys: C/c send test CAN TX, B/b request route start.\n");

    while (true) {
        can_frame_t frame;
        bool new_pose_sample = false;
        if (encoders_ready && encoders_read(&encoder_left_count, &encoder_right_count)) {
            if (encoder_count_baseline) {
                encoder_left_window += encoder_left_count - encoder_last_left_count;
                encoder_right_window += encoder_right_count - encoder_last_right_count;
            } else {
                encoder_count_baseline = true;
            }
            encoder_last_left_count = encoder_left_count;
            encoder_last_right_count = encoder_right_count;
        }
        if (mcp2515_read_message(&frame)) {
            print_can_frame("RX", &frame);
            if (lp_unpack(&frame, &pose)) {
                have_pose = true;
                last_can_rx = get_absolute_time();
                if (!have_sequence || pose.seq != last_sequence) {
                    last_sequence = pose.seq;
                    have_sequence = true;
                    last_sequence_progress = get_absolute_time();
                    new_pose_sample = true;
                }
                printf("RX POSE x=%d mm y=%d mm angle=%.1f deg status=0x%02X seq=%u\n",
                       pose.x_mm, pose.y_mm, pose.angle_ddeg / 10.0f,
                       pose.status, pose.seq);
            }
        }

        int terminal_command = getchar_timeout_us(0);
        bool test_pressed = button_check(&button_c) == 1 ||
            terminal_command == 'c' || terminal_command == 'C';
        bool start_pressed = button_check(&button_b) == 1 ||
            terminal_command == 'b' || terminal_command == 'B';
        absolute_time_t now = get_absolute_time();
        if (state == CAN_MONITOR && test_pressed) {
            uint8_t sequence = diagnostic_sequence++;
            send_can_diagnostic(sequence);
        }
        bool can_alive = have_pose &&
            absolute_time_diff_us(last_can_rx, now) < ROUTE_CAN_TIMEOUT_MS * 1000;
        bool sequence_alive = have_sequence &&
            absolute_time_diff_us(last_sequence_progress, now) < ROUTE_SEQUENCE_TIMEOUT_MS * 1000;
        bool pose_fresh = can_alive && sequence_alive && pose_usable(&pose);

        if (state == CAN_MONITOR && start_pressed) {
            if (!ROUTE_CALIBRATED) {
                printf("Start refused: waypoint table is uncalibrated.\n");
            } else if (!encoders_ready) {
                printf("Start refused: wheel encoder initialization failed.\n");
            } else if (!pose_fresh) {
                printf("Start refused: waiting for valid, fresh CAN pose sequence.\n");
            } else {
                uint8_t start_id = route_order[0];
                float start_distance = distance_to(&pose, waypoint_point(start_id));
                if (start_distance <= ROUTE_START_RADIUS_MM) {
                    target_order_index = 1;
                    encoder_left_window = encoder_right_window = 0;
                    encoder_trim_left = encoder_trim_right = 0;
                    encoder_integral_left = encoder_integral_right = 0.0f;
                    encoder_feedback_active = false;
                    last_encoder_feedback = now;
                    state = RUNNING;
                    printf("Route start accepted at P14; proceeding toward P1.\n");
                } else {
                    printf("Start refused: P14 distance %.0f mm (limit %u mm).\n",
                           start_distance, ROUTE_START_RADIUS_MM);
                }
            }
        }

        if (state == RUNNING || state == PAUSED_DATA_LOSS) {
            if (!pose_fresh) {
                if (state != PAUSED_DATA_LOSS) printf("Pose/sequence stale; stopping motors.\n");
                state = PAUSED_DATA_LOSS;
                left = right = 0;
                motors_set_speeds(0, 0);
                encoder_trim_left = encoder_trim_right = 0;
                encoder_integral_left = encoder_integral_right = 0.0f;
                encoder_feedback_active = false;
                encoder_left_window = encoder_right_window = 0;
            } else {
                if (state == PAUSED_DATA_LOSS) {
                    state = RUNNING;
                    printf("Fresh pose sequence restored; continuing toward current coordinate.\n");
                }

                if (new_pose_sample && target_order_index < ROUTE_ORDER_COUNT) {
                    absolute_time_t feedback_now = get_absolute_time();
                    int32_t feedback_left_delta = encoder_left_window;
                    int32_t feedback_right_delta = encoder_right_window;
                    if (encoder_feedback_active) {
                        int32_t feedback_elapsed_ms = (int32_t)(
                            absolute_time_diff_us(last_encoder_feedback, feedback_now) / 1000);
                        update_encoder_trim(left, right,
                                            feedback_left_delta, feedback_right_delta,
                                            feedback_elapsed_ms,
                                            &encoder_integral_left, &encoder_integral_right,
                                            &encoder_trim_left, &encoder_trim_right);
                    } else {
                        encoder_feedback_active = true;
                    }
                    encoder_left_window = encoder_right_window = 0;
                    last_encoder_feedback = feedback_now;
                    uint8_t target_id = route_order[target_order_index];
                    point_t target = waypoint_point(target_id);
                    float distance = distance_to(&pose, target);

                    bool captured_waypoint = false;
                    if (distance <= ROUTE_CAPTURE_RADIUS_MM) {
                        printf("Captured %s in order (seq=%u).\n",
                               route_waypoints[target_id].name, pose.seq);
                        ++target_order_index;
                        encoder_trim_left = encoder_trim_right = 0;
                        encoder_integral_left = encoder_integral_right = 0.0f;
                        captured_waypoint = true;
                        left = right = 0;
                        motors_set_speeds(0, 0);
                        if (target_order_index >= ROUTE_ORDER_COUNT) {
                            state = DONE;
                            left = right = 0;
                            motors_set_speeds(0, 0);
                            printf("One ordered lap complete at P14. Motors stopped.\n");
                        }
                    }

                    if (state == RUNNING && !captured_waypoint) {
                        /* Aim directly from the current camera pose to the next waypoint. */
                        float bearing = radians_to_ddeg(atan2f(
                            target.y - pose.y_mm, target.x - pose.x_mm));
                        int32_t heading_error = wrap_ddeg((int32_t)lroundf(bearing) -
                                                          pose.angle_ddeg);
                        int32_t turn = (int32_t)lroundf(-heading_error * ROUTE_HEADING_KP);
                        if (turn > ROUTE_MAX_TURN_CORRECTION)
                            turn = ROUTE_MAX_TURN_CORRECTION;
                        if (turn < -ROUTE_MAX_TURN_CORRECTION)
                            turn = -ROUTE_MAX_TURN_CORRECTION;

                        int base = distance < ROUTE_NEAR_DISTANCE_MM ?
                                   ROUTE_NEAR_SPEED : ROUTE_CRUISE_SPEED;
                        /* Pivot first for a large bearing error; otherwise drive an arc. */
                        bool pivoting = abs(heading_error) > 350;
                        if (pivoting) {
                            /* Static friction needs full pivot PWM; don't let wheel trim
                             * turn the fixed pivot command into an uneven command. */
                            turn = heading_error > 0 ? -ROUTE_PIVOT_SPEED : ROUTE_PIVOT_SPEED;
                            left = turn;
                            right = -left;
                            encoder_trim_left = encoder_trim_right = 0;
                            encoder_integral_left = encoder_integral_right = 0.0f;
                        } else {
                            int32_t requested_left = clamp_speed(base + turn);
                            int32_t requested_right = clamp_speed(base - turn);
                            left = apply_encoder_trim(requested_left, encoder_trim_left);
                            right = apply_encoder_trim(requested_right, encoder_trim_right);
                        }
                        motors_set_speeds(left, right);

                        if (absolute_time_diff_us(last_report, now) >= 200000) {
                            last_report = now;
                            long seq_age_ms = (long)(absolute_time_diff_us(last_sequence_progress, now) / 1000);
                            printf("%s target=%s seq=%u seq_age=%ldms x=%d y=%d distance=%.0f bearing=%.1f heading_error=%.1f base=%d turn=%ld enc=%ld,%ld trim=%ld,%ld cmd=%ld,%ld\n",
                                   state_name(state),
                                   route_waypoints[target_id].name,
                                   pose.seq, seq_age_ms, pose.x_mm, pose.y_mm, distance,
                                   bearing / 10.0f, heading_error / 10.0f, base, (long)turn,
                                   (long)feedback_left_delta, (long)feedback_right_delta,
                                   (long)encoder_trim_left, (long)encoder_trim_right,
                                   (long)left, (long)right);
                        }
                    }
                }
            }
        }

        if (state == DONE) {
            motors_set_speeds(0, 0);
        } else if (state == CAN_MONITOR &&
                   absolute_time_diff_us(last_report, now) >= 750000) {
            last_report = now;
            printf("%s | CAN=%s seq=%u\n", state_name(state),
                   pose_fresh ? "fresh" : "stale", pose.seq);
        }
        sleep_ms(ROUTE_LOOP_PERIOD_MS);
    }
}
