/* AUDIO_CMD: la Pico 2 W le indica al Zumo el campo de cultivo elegido (REQ-AUD-10).
 * ID 0x102 (el 0x101 es LP_DATA).  Big-endian, DLC 4:
 *   D0 campo (1 = tono 700 Hz, 2 = tono 1000 Hz)   D1 SNR estimada [dB] int8
 *   D2 amplitud del tono [1/255 de escala completa]  D3 seq (+1 por comando)   */
#ifndef AUDIO_CAN_H
#define AUDIO_CAN_H
#include <stdint.h>
#include "mcp2515.h"
#define AUDIO_CAN_ID   0x102
#define AUDIO_CAN_DLC  4

static inline void audio_pack(can_frame_t *f, uint8_t field, int snr_db, float mag, uint8_t seq) {
    if (snr_db > 99) snr_db = 99;
    if (snr_db < -99) snr_db = -99;
    int m = (int)(mag * 255.0f + 0.5f); if (m > 255) m = 255;
    f->id = AUDIO_CAN_ID; f->dlc = AUDIO_CAN_DLC;
    f->data[0] = field; f->data[1] = (uint8_t)(int8_t)snr_db; f->data[2] = (uint8_t)m; f->data[3] = seq;
}
static inline int audio_unpack(const can_frame_t *f, uint8_t *field, int *snr_db, float *mag, uint8_t *seq) {
    if (f->id != AUDIO_CAN_ID || f->dlc != AUDIO_CAN_DLC) return 0;
    *field = f->data[0]; *snr_db = (int8_t)f->data[1]; *mag = f->data[2] / 255.0f; *seq = f->data[3];
    return 1;
}
#endif
