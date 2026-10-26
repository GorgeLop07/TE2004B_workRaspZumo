#ifndef LP_CAN_H
#define LP_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "mcp2515.h"

/* LP_DATA frame (big-endian):
 * D0-D1 x [mm], D2-D3 y [mm], D4-D5 angle [0.1 deg], D6 status, D7 seq */
#define LP_CAN_ID        0x101
#define LP_CAN_DLC       8

#define LP_STATUS_VALID  0x01   // marker seen at least once
#define LP_STATUS_FRESH  0x02   // last update newer than the Pico timeout

typedef struct {
    int16_t x_mm;
    int16_t y_mm;
    int16_t angle_ddeg;
    uint8_t status;
    uint8_t seq;
} lp_data_t;

static inline void lp_pack(const lp_data_t *lp, can_frame_t *frame) {
    frame->id = LP_CAN_ID;
    frame->dlc = LP_CAN_DLC;
    frame->data[0] = (uint8_t)((uint16_t)lp->x_mm >> 8);
    frame->data[1] = (uint8_t)((uint16_t)lp->x_mm & 0xFF);
    frame->data[2] = (uint8_t)((uint16_t)lp->y_mm >> 8);
    frame->data[3] = (uint8_t)((uint16_t)lp->y_mm & 0xFF);
    frame->data[4] = (uint8_t)((uint16_t)lp->angle_ddeg >> 8);
    frame->data[5] = (uint8_t)((uint16_t)lp->angle_ddeg & 0xFF);
    frame->data[6] = lp->status;
    frame->data[7] = lp->seq;
}

static inline bool lp_unpack(const can_frame_t *frame, lp_data_t *lp) {
    if (frame->id != LP_CAN_ID || frame->dlc != LP_CAN_DLC) return false;
    lp->x_mm = (int16_t)(((uint16_t)frame->data[0] << 8) | frame->data[1]);
    lp->y_mm = (int16_t)(((uint16_t)frame->data[2] << 8) | frame->data[3]);
    lp->angle_ddeg = (int16_t)(((uint16_t)frame->data[4] << 8) | frame->data[5]);
    lp->status = frame->data[6];
    lp->seq = frame->data[7];
    return true;
}

#endif // LP_CAN_H
