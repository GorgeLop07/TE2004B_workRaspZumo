/* Adquisicion determinista del MAX4466 (REQ-AUD-02).
 * El reloj del ADC (48 MHz) dividido por un valor fijo marca cada muestra en
 * hardware, y un DMA "sin fin" las deposita en un bufer circular en RAM. La CPU
 * no participa en el muestreo, asi que el periodo Ts = 1/Fs es constante sin
 * importar lo que haga el resto del firmware (USB, CAN, filtros). */
#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H
#include <stdint.h>

#define AUDIO_RING_SAMPLES 2048          /* 256 ms de holgura a 8 kHz */

/* gpio: 26..29 (ADC0..ADC3). Ver nota de GP29 en README. */
void     audio_input_init(unsigned gpio, unsigned fs_hz);
/* Copia hasta `max` muestras de 12 bits sin leer; devuelve cuantas. */
uint32_t audio_input_pull(uint16_t *dst, uint32_t max);
uint32_t audio_input_backlog(void);      /* muestras pendientes de leer */
void     audio_input_flush(void);        /* descarta lo pendiente */
uint32_t audio_input_overruns(void);     /* veces que el retraso paso de media vuelta del bufer */
#endif
