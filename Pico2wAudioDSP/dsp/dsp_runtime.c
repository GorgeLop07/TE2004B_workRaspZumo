#include <math.h>
#include <string.h>
#include "dsp_runtime.h"
#include "dsp_coeffs.h"

uint64_t dsp_mac_count = 0;
#ifdef DSP_COUNT_MAC
#define MACS(n) (dsp_mac_count += (n))
#else
#define MACS(n) ((void)0)
#endif

static float g_hann[DSP_GOERTZEL_N];
static float g_gcoef[DSP_NTONES];   /* 2 cos(2 pi f / Fs) */

void dsp_global_init(void) {
    for (int n = 0; n < DSP_GOERTZEL_N; n++)
        g_hann[n] = 0.5f * (1.0f - cosf(6.28318530718f * (float)n / (float)DSP_GOERTZEL_N));
    for (int t = 0; t < DSP_NTONES; t++)
        g_gcoef[t] = 2.0f * cosf(6.28318530718f * dsp_tone_hz[t] / (float)DSP_FS_HZ);
}

/* ---- bloqueador de DC: y[n] = x[n] - x[n-1] + R y[n-1]  (1 MAC) ---- */
float dsp_dc_step(dsp_dc_t *s, float x) {
    float y = x - s->x1 + DSP_DC_R * s->y1;
    s->x1 = x; s->y1 = y;
    MACS(1);
    return y;
}

/* ---- FIR, forma directa I, bufer circular ----
 * buf[pos] guarda x[n]; h[k] multiplica x[n-k] = buf[(pos-k) mod N].
 * Dos bucles sin modulo (primero hacia atras hasta 0, luego desde el final). */
float dsp_fir_step(dsp_fir_t *s, float x) {
    const int N = DSP_FIR_TAPS;
    int p = s->pos;
    s->buf[p] = x;
    float acc = 0.0f;
    int k = 0;
    for (int i = p; i >= 0; i--, k++) acc += dsp_fir_h[k] * s->buf[i];
    for (int i = N - 1; i > p; i--, k++) acc += dsp_fir_h[k] * s->buf[i];
    s->pos = (p + 1 == N) ? 0 : p + 1;
    MACS(N);
    return acc;
}

/* ---- banco IIR: biquads en forma directa II ---- */
float dsp_iir_step(dsp_iir_t *s, float x) {
    float sum = 0.0f;
    for (int t = 0; t < DSP_NTONES; t++) {
        float v = x;
        for (int q = 0; q < DSP_IIR_NSEC; q++) {
            int i = t * DSP_IIR_NSEC + q;
            const float *c = dsp_iir_sos[i];
            float w = v - c[3] * s->w1[i] - c[4] * s->w2[i];
            v = c[0] * w + c[1] * s->w1[i] + c[2] * s->w2[i];
            s->w2[i] = s->w1[i];
            s->w1[i] = w;
        }
        sum += v;
    }
    MACS(DSP_NTONES * DSP_IIR_NSEC * 5);
    return sum;
}

/* ---- Goertzel: s0 = xw + c s1 - s2 ; |X|^2 = s1^2 + s2^2 - c s1 s2 ---- */
void dsp_goertzel_reset(dsp_goertzel_t *g) { memset(g, 0, sizeof *g); }

static inline void goertzel_push(dsp_goertzel_t *g, float y) {
    float xw = y * g_hann[g->n];
    for (int t = 0; t < DSP_NTONES; t++) {
        float s0 = xw + g_gcoef[t] * g->s1[t] - g->s2[t];
        g->s2[t] = g->s1[t];
        g->s1[t] = s0;
    }
    g->n++;
    MACS(1 + DSP_NTONES);
}

static void goertzel_finish(dsp_goertzel_t *g, float *mag) {
    for (int t = 0; t < DSP_NTONES; t++) {
        float p = g->s1[t] * g->s1[t] + g->s2[t] * g->s2[t] - g_gcoef[t] * g->s1[t] * g->s2[t];
        if (p < 0.0f) p = 0.0f;
        /* Hann: suma de la ventana = N/2  =>  |X| = A N / 4 */
        mag[t] = 4.0f * sqrtf(p) / (float)DSP_GOERTZEL_N;
    }
    dsp_goertzel_reset(g);
}

void dsp_pipeline_init(dsp_pipeline_t *p) { memset(p, 0, sizeof *p); }

bool dsp_pipeline_push(dsp_pipeline_t *p, float x) {
    float xd = dsp_dc_step(&p->dc, x);
    float yf = dsp_fir_step(&p->fir, xd);
    float yi = dsp_iir_step(&p->iir, xd);
    goertzel_push(&p->g[DSP_CHAIN_RAW], xd);
    goertzel_push(&p->g[DSP_CHAIN_FIR], yf);
    goertzel_push(&p->g[DSP_CHAIN_IIR], yi);
    p->raw_energy += xd * xd;
    if (++p->n < DSP_GOERTZEL_N) return false;
    for (int c = 0; c < DSP_NCHAINS; c++) {
        goertzel_finish(&p->g[c], p->blk[c].mag);
        p->blk[c].p_raw = p->raw_energy / (float)DSP_GOERTZEL_N;
    }
    p->raw_energy = 0.0f;
    p->n = 0;
    return true;
}

/* ---- Deteccion y validacion (REQ-AUD-08) ---- */
void dsp_det_init(dsp_detector_t *d) {
    memset(d, 0, sizeof *d);
    d->cal_left = DSP_CAL_BLOCKS;
    for (int t = 0; t < DSP_NTONES; t++) d->thr[t] = 1e9f;
}

bool dsp_det_calibrated(const dsp_detector_t *d) { return d->cal_left == 0; }

bool dsp_det_update(dsp_detector_t *d, const dsp_block_t *b, dsp_event_t *ev) {
    /* Fase de calibracion: media y desviacion (Welford) de la amplitud en cada
     * tono con solo ruido ambiente. Umbral = max(piso, media + K sigma). */
    if (d->cal_left > 0) {
        d->cal_count++;
        for (int t = 0; t < DSP_NTONES; t++) {
            float delta = b->mag[t] - d->cal_mean[t];
            d->cal_mean[t] += delta / (float)d->cal_count;
            d->cal_m2[t] += delta * (b->mag[t] - d->cal_mean[t]);
        }
        if (--d->cal_left == 0) {
            for (int t = 0; t < DSP_NTONES; t++) {
                float sd = sqrtf(d->cal_m2[t] / (float)(d->cal_count > 1 ? d->cal_count - 1 : 1));
                float thr = d->cal_mean[t] + DSP_THR_K * sd;
                d->thr[t] = thr > DSP_AMP_FLOOR ? thr : DSP_AMP_FLOOR;
            }
        }
        d->last_reason = DSP_REJ_CALIBRATING;
        return false;
    }
    if (d->holdoff > 0) { d->holdoff--; d->last_reason = DSP_REJ_HOLDOFF; return false; }

    int best = 0;
    for (int t = 1; t < DSP_NTONES; t++) if (b->mag[t] > b->mag[best]) best = t;
    float other = 0.0f;
    for (int t = 0; t < DSP_NTONES; t++) if (t != best && b->mag[t] > other) other = b->mag[t];

    /* SNR estimada de banda ancha: potencia del tono frente al resto. */
    float p_tone = 0.5f * b->mag[best] * b->mag[best];
    float p_noise = b->p_raw - p_tone;
    float snr_db = (p_noise <= 1e-12f) ? 99.0f : 10.0f * log10f(p_tone / p_noise + 1e-12f);

    dsp_reason_t why = DSP_REJ_NONE;
    if (b->mag[best] < d->thr[best])              why = DSP_REJ_BELOW_THR;   /* magnitud */
    else if (other >= DSP_AMBIG_RATIO * b->mag[best]) why = DSP_REJ_AMBIGUOUS; /* dos tonos parecidos */
    else if (snr_db < DSP_SNR_MIN_DB)             why = DSP_REJ_LOW_SNR;     /* SNR bajo el margen */
    d->last_reason = why; d->last_best = best + 1; d->last_snr_db = snr_db;

    if (why != DSP_REJ_NONE) { d->run_len = 0; d->run_field = 0; return false; }

    /* Duracion minima: DSP_MIN_BLOCKS bloques consecutivos con el mismo tono. */
    if (d->run_field == best + 1) {
        d->run_len++; d->run_mag += b->mag[best]; d->run_snr += snr_db;
    } else {
        d->run_field = best + 1; d->run_len = 1; d->run_mag = b->mag[best]; d->run_snr = snr_db;
    }
    if (d->run_len >= DSP_MIN_BLOCKS) {
        if (ev) { ev->field = d->run_field; ev->mag = d->run_mag / d->run_len; ev->snr_db = d->run_snr / d->run_len; }
        d->run_len = 0; d->run_field = 0; d->holdoff = DSP_HOLDOFF_BLOCKS;
        return true;
    }
    return false;
}
