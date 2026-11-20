/* Parametros del modulo DSP (REQ-AUD-01/02/04/05/07/08).
 * Este archivo lo comparten el firmware de la Pico 2 W, el generador de
 * coeficientes y el banco de pruebas de la PC: un solo lugar para cambiar
 * tonos, Fs u ordenes. Si se cambia algo aqui hay que regenerar
 * dsp/dsp_coeffs.h con `make -C host gen`. */
#ifndef DSP_CONFIG_H
#define DSP_CONFIG_H

/* REQ-AUD-02: muestreo determinista. Fs = 8 kHz -> Nyquist 4 kHz, mucho
 * mayor que el tono mas alto (1 kHz) y sus armonicos 2do y 3ro. */
#define DSP_FS_HZ            8000

/* REQ-AUD-01: un tono por campo. Separacion 300 Hz (>= 150 Hz), ambos > 100 Hz.
 * Campo 1 (A) = 700 Hz, campo 2 (B) = 1000 Hz. Con N = 400 caen en los
 * bins 35 y 50 de Goertzel (resolucion 20 Hz). */
#define DSP_NTONES           2
#define DSP_TONE_HZ_INIT     { 700.0, 1000.0 }

/* REQ-AUD-04: FIR multibanda, ventana Hamming, M = 256 (257 coeficientes). */
#define DSP_FIR_M            256
#define DSP_FIR_TAPS         (DSP_FIR_M + 1)
#define DSP_FIR_HALFBW_HZ    50.0      /* banda de paso: tono +/- 50 Hz */

/* REQ-AUD-05: banco IIR. Prototipo Butterworth de orden N (pasabajas) ->
 * pasabanda de orden 2N = N biquads por tono, en cascada; tonos en paralelo. */
#define DSP_IIR_LP_ORDER     2
#define DSP_IIR_NSEC         DSP_IIR_LP_ORDER
#define DSP_IIR_BW_HZ        100.0     /* ancho de banda -3 dB por tono */

/* Acondicionamiento previo: bloqueador de DC  y[n] = x[n]-x[n-1]+R*y[n-1]. */
#define DSP_DC_R             0.995f

/* REQ-AUD-07: bloque de Goertzel (50 ms) con ventana Hann. */
#define DSP_GOERTZEL_N       400

/* REQ-AUD-08: validacion. Amplitudes normalizadas a escala completa = 1.0. */
#define DSP_CAL_BLOCKS       40        /* calibracion: 2 s de ruido ambiente   */
#define DSP_THR_K            6.0f      /* umbral = media + K*sigma del ruido   */
#define DSP_AMP_FLOOR        0.01f     /* piso absoluto del umbral             */
#define DSP_AMBIG_RATIO      0.5f      /* otro tono >= 50 % del mayor -> ambiguo */
#define DSP_SNR_MIN_DB       (-10.0f)  /* margen operativo minimo de SNR       */
#define DSP_MIN_BLOCKS       4         /* duracion minima: 4 bloques = 200 ms  */
#define DSP_HOLDOFF_BLOCKS   30        /* tras un comando, ignora 1.5 s        */

#endif /* DSP_CONFIG_H */
