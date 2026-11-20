/* Modulo DSP: deteccion robusta de tonos de comando con el MAX4466 en la Pico 2 W
 * y envio del campo elegido al Zumo por CAN (ID 0x102).
 *
 *  Comandos por el monitor serial (115200):
 *    h ayuda   b benchmark   c recalibrar   t autoprueba AWGN en la Pico
 *    r/f/i  decide con: sin filtro / FIR / IIR      v  imprime cada bloque
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "mcp2515.h"
#include "audio_can.h"
#include "audio_input.h"
#include "dsp_runtime.h"
#include "dsp_coeffs.h"

/* ---- Pin del microfono -------------------------------------------------
 * ATENCION: en la Pico 2 W el GP29 NO sale al header: es ADC3 conectado al divisor
 * de VSYS y comparte linea con el reloj del chip WiFi. Los pines ADC accesibles
 * son GP26 (ADC0), GP27 (ADC1) y GP28 (ADC2). Si la senal no se ve bien en el
 * monitor ('DC' fuera de ~2048 o 'rms' ~0), cambiar a 26. */
#define MIC_GPIO        29
#define SAMPLE_CHUNK    256
#define WARM_BLOCKS     20      /* 1 s para que se asienten el bloqueo DC y los filtros */

static const char *CHAIN_NAME[DSP_NCHAINS] = { "SIN FILTRO", "FIR", "IIR" };

static dsp_pipeline_t pipe;
static dsp_detector_t det[DSP_NCHAINS];
static int decide = DSP_CHAIN_FIR;
static bool verbose = false, can_ok = false;
static int warm = WARM_BLOCKS;

static void restart_calibration(void) {
    for (int c = 0; c < DSP_NCHAINS; c++) dsp_det_init(&det[c]);
    dsp_pipeline_init(&pipe);
    warm = WARM_BLOCKS;
    printf("[CAL] calibrando con el ruido ambiente (%d s de calma, sin tonos)...\n", (WARM_BLOCKS + DSP_CAL_BLOCKS) * DSP_GOERTZEL_N / DSP_FS_HZ);
}

/* ------------------------------------------------------------ benchmark -- */
static float bench_in[4000];
static volatile float bench_sink;

static uint32_t lcg_state = 12345;
static float lcg_noise(void) { lcg_state = lcg_state * 1664525u + 1013904223u; return ((int32_t)lcg_state >> 8) * (1.0f / 8388608.0f); }

#define TIME_BLOCK(name, macs, state_bytes, stmt) do {                                      \
    uint64_t t0 = time_us_64(); float acc = 0;                                              \
    for (int rep = 0; rep < 4; rep++) for (int i = 0; i < 4000; i++) { float v = bench_in[i]; stmt; } \
    uint64_t dt = time_us_64() - t0; bench_sink = acc;                                      \
    float us = (float)dt / 16000.0f;                                                        \
    printf("%-26s %9.3f %10d %9.2f %9u\n", name, us, (int)(macs), us * DSP_FS_HZ / 1e4f, (unsigned)(state_bytes)); \
} while (0)

static void run_benchmark(void) {
    printf("\n=== Benchmark Pico 2 W @ %u MHz (RP2350, float32) - 16000 muestras por bloque ===\n", (unsigned)(clock_get_hz(clk_sys) / 1000000));
    for (int i = 0; i < 4000; i++) bench_in[i] = 0.05f * sinf(6.2831853f * 700.0f * i / DSP_FS_HZ) + 0.1f * lcg_noise();
    dsp_dc_t dc = {0}; dsp_fir_t fir = {0}; dsp_iir_t iir = {0}; dsp_pipeline_t pl; dsp_pipeline_init(&pl);
    printf("%-26s %9s %10s %9s %9s\n", "Bloque", "us/muestra", "MAC/muest", "CPU[%]", "RAM[B]");
    TIME_BLOCK("bloqueo DC", 1, sizeof dc, acc += dsp_dc_step(&dc, v));
    TIME_BLOCK("FIR multibanda (M=256)", DSP_FIR_TAPS, sizeof fir, acc += dsp_fir_step(&fir, v));
    TIME_BLOCK("banco IIR (2x2 biquads)", DSP_NTONES * DSP_IIR_NSEC * 5, sizeof iir, acc += dsp_iir_step(&iir, v));
    TIME_BLOCK("tuberia completa", 1 + DSP_FIR_TAPS + DSP_NTONES * DSP_IIR_NSEC * 5 + DSP_NCHAINS * (1 + DSP_NTONES), sizeof pl, acc += dsp_pipeline_push(&pl, v));
    printf("Coeficientes en flash: FIR %u B, IIR %u B, ventana Hann (RAM) %u B\n",
           (unsigned)sizeof dsp_fir_h, (unsigned)sizeof dsp_iir_sos, (unsigned)(sizeof(float) * DSP_GOERTZEL_N));
    printf("Periodo de muestreo disponible: %.1f us (Fs=%d Hz)\n", 1e6f / DSP_FS_HZ, DSP_FS_HZ);
    audio_input_flush();
}

/* ------------------------------------------------------- autoprueba AWGN -- */
static float gauss(void) {
    float u1 = (lcg_noise() * 0.5f + 0.5f) * 0.999998f + 1e-6f, u2 = lcg_noise() * 0.5f + 0.5f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

static void run_selftest(void) {
    const float sigma = 0.10f;
    const float snrs[] = { 10, 5, 0, -5, -8, -10 };
    const int trials = 10;
    printf("\n=== Autoprueba en la Pico: tono + AWGN (sigma=%.2f), %d pruebas por punto ===\n", sigma, trials);
    dsp_pipeline_t p; dsp_pipeline_init(&p);
    dsp_detector_t cal[DSP_NCHAINS]; for (int c = 0; c < DSP_NCHAINS; c++) dsp_det_init(&cal[c]);
    int blocks = 0;
    for (int i = 0; i < (20 + DSP_CAL_BLOCKS + 1) * DSP_GOERTZEL_N; i++)
        if (dsp_pipeline_push(&p, 0.03f + sigma * gauss()) && ++blocks > 20)
            for (int c = 0; c < DSP_NCHAINS; c++) dsp_det_update(&cal[c], &p.blk[c], NULL);
    printf("SNR[dB] | sin filtro  FIR   IIR  (aciertos de %d)\n", 2 * trials);
    for (unsigned s = 0; s < sizeof snrs / sizeof *snrs; s++) {
        float A = sqrtf(2.0f * powf(10.0f, snrs[s] / 10.0f)) * sigma;
        int ok[DSP_NCHAINS] = {0};
        for (int t = 0; t < 2 * trials; t++) {
            int field = 1 + (t & 1); float f0 = dsp_tone_hz[field - 1];
            dsp_pipeline_t tp; dsp_pipeline_init(&tp);
            dsp_detector_t d[DSP_NCHAINS]; memcpy(d, cal, sizeof d);
            int onset = 20 * DSP_GOERTZEL_N + (int)(lcg_noise() * 0.5f * DSP_GOERTZEL_N + DSP_GOERTZEL_N / 2);
            int total = onset + 24 * DSP_GOERTZEL_N; bool got[DSP_NCHAINS] = {0};
            for (int i = 0; i < total; i++) {
                float x = 0.03f + sigma * gauss();
                if (i >= onset) x += A * sinf(6.2831853f * f0 * (float)i / DSP_FS_HZ);
                if (dsp_pipeline_push(&tp, x))
                    for (int c = 0; c < DSP_NCHAINS; c++) {
                        dsp_event_t ev;
                        if (dsp_det_update(&d[c], &tp.blk[c], &ev) && i >= onset && !got[c]) { got[c] = true; if (ev.field == field) ok[c]++; }
                    }
            }
        }
        printf("  %+5.0f | %8d %6d %5d\n", snrs[s], ok[0], ok[1], ok[2]);
    }
    audio_input_flush();
}

/* ------------------------------------------------------------------ main -- */
int main(void) {
    stdio_init_all();
    sleep_ms(3000);   /* da tiempo a que enumere el USB serial */
    printf("\n--- Pico 2 W: modulo DSP de audio (MAX4466 en GP%d) ---\n", MIC_GPIO);
    printf("Fs=%d Hz, tonos:", DSP_FS_HZ);
    for (int t = 0; t < DSP_NTONES; t++) printf(" campo %d = %.0f Hz;", t + 1, dsp_tone_hz[t]);
    printf("\n");

    mcp2515_init();
    if (mcp2515_spi_selftest() && mcp2515_set_bitrate_500kbps_8mhz() && mcp2515_set_mode(MCP2515_MODE_NORMAL)) {
        can_ok = true;
        printf("CAN listo (NORMAL, 500 kbps). Comandos se envian con ID 0x%03X\n", AUDIO_CAN_ID);
    } else {
        printf("CAN NO disponible: se detecta pero no se envia nada (revisar SPI)\n");
    }

    dsp_global_init();
    audio_input_init(MIC_GPIO, DSP_FS_HZ);
    restart_calibration();
    printf("Escribe 'h' para ver los comandos.\n");

    uint16_t raw[SAMPLE_CHUNK];
    uint64_t t_rep = time_us_64(), busy_us = 0; uint32_t n_rep = 0;
    double sum_adc = 0; uint32_t n_adc = 0, clip = 0; uint8_t seq = 0;
    int events[DSP_NCHAINS] = {0};

    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            switch (ch) {
                case 'b': run_benchmark(); restart_calibration(); break;
                case 't': run_selftest(); restart_calibration(); break;
                case 'c': restart_calibration(); break;
                case 'r': decide = DSP_CHAIN_RAW; printf("Decide con: %s\n", CHAIN_NAME[decide]); break;
                case 'f': decide = DSP_CHAIN_FIR; printf("Decide con: %s\n", CHAIN_NAME[decide]); break;
                case 'i': decide = DSP_CHAIN_IIR; printf("Decide con: %s\n", CHAIN_NAME[decide]); break;
                case 'v': verbose = !verbose; break;
                default:  printf("b benchmark | t autoprueba AWGN | c recalibrar | r/f/i decide sin filtro/FIR/IIR | v verbose\n");
            }
        }

        uint32_t n = audio_input_pull(raw, SAMPLE_CHUNK);
        if (n) {
            uint64_t t0 = time_us_64();
            for (uint32_t k = 0; k < n; k++) {
                sum_adc += raw[k]; n_adc++;
                if (raw[k] <= 4 || raw[k] >= 4091) clip++;
                float x = ((float)raw[k] - 2048.0f) * (1.0f / 2048.0f);
                if (!dsp_pipeline_push(&pipe, x)) continue;
                if (warm > 0) { warm--; continue; }
                for (int c = 0; c < DSP_NCHAINS; c++) {
                    dsp_event_t ev;
                    bool was_cal = !dsp_det_calibrated(&det[c]);
                    bool hit = dsp_det_update(&det[c], &pipe.blk[c], &ev);
                    if (was_cal && dsp_det_calibrated(&det[c]) && c == decide)
                        printf("[CAL] listo. Umbrales (%s): %.4f @ %.0f Hz, %.4f @ %.0f Hz\n", CHAIN_NAME[c], det[c].thr[0], dsp_tone_hz[0], det[c].thr[1], dsp_tone_hz[1]);
                    if (!hit) continue;
                    events[c]++;
                    if (c != decide) continue;
                    printf("*** COMANDO campo %d (%.0f Hz)  amp=%.3f  SNR~%.1f dB  [%s]", ev.field, dsp_tone_hz[ev.field - 1], ev.mag, ev.snr_db, CHAIN_NAME[c]);
                    if (can_ok) {
                        can_frame_t f; audio_pack(&f, (uint8_t)ev.field, (int)lroundf(ev.snr_db), ev.mag, ++seq);
                        printf("  -> CAN 0x%03X %s\n", AUDIO_CAN_ID, mcp2515_send_message(&f) ? "enviado" : "FALLO (sin ACK; revisar bus)");
                    } else printf("\n");
                }
                if (verbose) {
                    const dsp_block_t *b = &pipe.blk[decide];
                    printf("  blk %s: A%.0f=%.4f A%.0f=%.4f (thr %.4f/%.4f) rms=%.4f razon=%d\n", CHAIN_NAME[decide], dsp_tone_hz[0], b->mag[0], dsp_tone_hz[1], b->mag[1],
                           det[decide].thr[0], det[decide].thr[1], sqrtf(b->p_raw), (int)det[decide].last_reason);
                }
            }
            busy_us += time_us_64() - t0; n_rep += n;
        }

        uint64_t now = time_us_64();
        if (now - t_rep >= 1000000) {
            float secs = (now - t_rep) / 1e6f;
            const dsp_block_t *b = &pipe.blk[decide];
            printf("[AUD] fs=%.1f Hz  CPU=%.1f%%  DC=%.0f cnt  rms=%.4f  clip=%lu  overruns=%lu | A%.0f=%.4f A%.0f=%.4f thr=%.4f/%.4f | ev S/F/I=%d/%d/%d\n",
                   n_rep / secs, 100.0f * busy_us / (now - t_rep), n_adc ? sum_adc / n_adc : 0.0, sqrtf(b->p_raw), (unsigned long)clip,
                   (unsigned long)audio_input_overruns(), dsp_tone_hz[0], b->mag[0], dsp_tone_hz[1], b->mag[1],
                   det[decide].thr[0] > 1e8f ? 0.0f : det[decide].thr[0], det[decide].thr[1] > 1e8f ? 0.0f : det[decide].thr[1],
                   events[0], events[1], events[2]);
            t_rep = now; busy_us = 0; n_rep = 0; sum_adc = 0; n_adc = 0; clip = 0;
        }
    }
}
