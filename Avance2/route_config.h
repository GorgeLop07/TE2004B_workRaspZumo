#ifndef ROUTE_CONFIG_H
#define ROUTE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Survey the actual track before setting ROUTE_CALIBRATED to 1.
 * Coordinates are camera marker coordinates in millimeters. The current
 * transport convention is camera x/y and angle in tenths of degrees; document
 * its origin, axis directions, zero direction, sign, and marker offset here.
 * The coordinate controller assumes angle zero points along +X and positive
 * angles turn counterclockwise. Confirm this camera convention before running.
 */
#define ROUTE_CALIBRATED             1
#define ROUTE_COORDINATE_NOTES       "UNSURVEYED: record origin, axes, angle convention, marker offset"
#define ROUTE_MARKER_AT_ROBOT_CENTER 0

#define ROUTE_WAYPOINT_COUNT         14
#define ROUTE_ORDER_COUNT            15

/* Conservative initial settings. Tune only after reliable full laps. */
#define ROUTE_CAPTURE_RADIUS_MM      100
#define ROUTE_START_RADIUS_MM        120
#define ROUTE_SEQUENCE_TIMEOUT_MS    300
#define ROUTE_CAN_TIMEOUT_MS         300
#define ROUTE_LOOP_PERIOD_MS         10
#define ROUTE_CRUISE_SPEED           1300
#define ROUTE_PIVOT_SPEED             1300
#define ROUTE_NEAR_SPEED             700
#define ROUTE_NEAR_DISTANCE_MM       250
#define ROUTE_HEADING_KP             0.45f /* PWM per tenth-degree */
#define ROUTE_MAX_TURN_CORRECTION    1300
#define ROUTE_MAX_SPEED              2000

/* Expected encoder counts per PWM unit over a 100 ms control interval. */
#define ROUTE_ENCODER_TICKS_PER_PWM_100MS 0.10f
#define ROUTE_ENCODER_KP              1.5f
#define ROUTE_ENCODER_KI              0.10f
#define ROUTE_ENCODER_MAX_TRIM        500
#define ROUTE_ENCODER_MAX_INTEGRAL    2500

typedef struct {
    const char *name;
    int16_t x_mm;
    int16_t y_mm;
} waypoint_t;

/* Replace every coordinate with a camera measurement on the real track.
 * Each waypoint is approached directly from the current camera pose. Segment
 * shape labels are intentionally absent: the controller follows coordinates.
 */
static const waypoint_t route_waypoints[ROUTE_WAYPOINT_COUNT] = {
    { "P1",  -390, 1100 },
    { "P2",  -190, 1130 },
    { "P3",  -250,  420 },
    { "P4",  -510,  250 },
    { "P5",   -90,  990 },
    { "P6",   110, 1130 },
    { "P7",    70,  430 },
    { "P8",   270,  290 },
    { "P9",   200,  980 },
    { "P10",  400, 1140 },
    { "P11",  420,  230 },
    { "P12",  370,   50 },
    { "P13", -220,   80 },
    { "P14", -420,  100 },
};

/* Start and finish are both P14. The final P14 capture is accepted only after
 * all intervening waypoints have been captured in order.
 */
static const uint8_t route_order[ROUTE_ORDER_COUNT] = {
    13, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
};

#endif
