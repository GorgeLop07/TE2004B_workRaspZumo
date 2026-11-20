/* Cadena DSP en tiempo real (C11, float32). La misma fuente se compila en la
 * Pico 2 W y en el banco de pruebas de la PC, asi lo que se mide en la PC es
 * exactamente el codigo que corre en el robot.
 *
 *   muestra ADC -> bloqueo DC -> +--> (sin filtro) ---------+
 *                                +--> FIR multibanda -------+--> Goertzel -> validacion
 *                                +--> banco IIR (paralelo) -+
 */
#ifndef DSP_RUNTIME_H
#define DSP_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include "dsp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DSP_CHAIN_RAW = 0, DSP_CHAIN_FIR = 1, DSP_CHAIN_IIR = 2, DSP_NCHAINS = 3 };

/* Contador de MACs instrumentado (solo si se compila con -DDSP_COUNT_MAC). */
extern uint64_t dsp_mac_count;

/* --- bloques individuales (expuestos para medir cada uno por separado) --- */
typedef struct { float x1, y1; } dsp_dc_t;
float dsp_dc_step(dsp_dc_t *s, float x);

/* FIR forma directa I con bufer circular: O(1) por muestra, sin memmove. */
typedef struct { float buf[DSP_FIR_TAPS]; int pos; } dsp_fir_t;
float dsp_fir_step(dsp_fir_t *s, float x);

/* Banco IIR: por tono N biquads en cascada (forma directa II); los tonos van en paralelo y se suman. */
typedef struct { float w1[DSP_NTONES * DSP_IIR_NSEC]; float w2[DSP_NTONES * DSP_IIR_NSEC]; } dsp_iir_t;
float dsp_iir_step(dsp_iir_t *s, float x);

/* --- Goertzel por bloque con ventana Hann --- */
typedef struct {
    float mag[DSP_NTONES];   /* amplitud estimada de cada tono (escala completa = 1.0) */
    float p_raw;             /* potencia media de la senal sin filtrar en el bloque    */
} dsp_block_t;

typedef struct {
    float s1[DSP_NTONES], s2[DSP_NTONES];
    int n;
} dsp_goertzel_t;

/* --- Detector con validacion (REQ-AUD-08) --- */
typedef enum { DSP_REJ_NONE = 0, DSP_REJ_BELOW_THR, DSP_REJ_AMBIGUOUS, DSP_REJ_LOW_SNR, DSP_REJ_CALIBRATING, DSP_REJ_HOLDOFF } dsp_reason_t;

typedef struct {
    int   field;      /* 1..DSP_NTONES */
    float mag;        /* amplitud media durante la deteccion */
    float snr_db;     /* SNR estimada (banda ancha) */
} dsp_event_t;

typedef struct {
    int   cal_left, cal_count;
    float cal_mean[DSP_NTONES], cal_m2[DSP_NTONES];
    float thr[DSP_NTONES];
    int   run_field, run_len, holdoff;
    float run_mag, run_snr;
    /* ultima decision de bloque, para depuracion/registro */
    dsp_reason_t last_reason;
    int   last_best;
    float last_snr_db;
} dsp_detector_t;

void dsp_det_init(dsp_detector_t *d);                       /* arranca una calibracion de DSP_CAL_BLOCKS */
bool dsp_det_calibrated(const dsp_detector_t *d);
bool dsp_det_update(dsp_detector_t *d, const dsp_block_t *b, dsp_event_t *ev); /* true = comando aceptado */

/* --- Tuberia completa: 1 llamada por muestra --- */
typedef struct {
    dsp_dc_t dc; dsp_fir_t fir; dsp_iir_t iir;
    dsp_goertzel_t g[DSP_NCHAINS];
    float raw_energy;
    int   n;
    dsp_block_t blk[DSP_NCHAINS];
} dsp_pipeline_t;

void dsp_global_init(void);                 /* tabla Hann y coeficientes de Goertzel (una vez) */
void dsp_pipeline_init(dsp_pipeline_t *p);
/* x en [-1,1] (con DC permitido). Devuelve true cuando se completo un bloque (p->blk[] valido). */
bool dsp_pipeline_push(dsp_pipeline_t *p, float x);

/* Para el banco de pruebas: filtros sueltos sobre la senal ya sin DC. */
void dsp_goertzel_reset(dsp_goertzel_t *g);

#ifdef __cplusplus
}
#endif
#endif
