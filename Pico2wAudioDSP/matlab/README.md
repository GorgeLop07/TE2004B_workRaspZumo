# Versión MATLAB del módulo DSP

Réplica didáctica de la cadena que corre en la Pico 2 W, para **ver y entender** cada etapa en la PC.
Solo usa MATLAB base (sin Signal Processing Toolbox) y respeta la regla del reto: **ningún** `butter`,
`fir1`, `designfilt`, `freqz`, `pwelch` ni `resample`; el diseño, el Welch y el remuestreo están escritos a mano.

## Cómo correrlo

```matlab
cd matlab
verify_against_c     % 1) comprueba que el diseño == coeficientes de la Pico (dsp_coeffs.h)
demo_dsp             % 2) recorrido paso a paso con 5 figuras (out/m1…m5)
stress_test(100)     % 3) curva SNR vs aciertos (out/m6_estres_snr.png); 100–200 pruebas por punto
live_demo            % 4) DEMO EN TIEMPO REAL con audio: escuchas los tonos y los filtros (ver abajo)
explain_order_M      % 5) de dónde sale el orden M del FIR, con barrido medido (out/m7_orden_M.png)
explain_iir          % 6) REQ-AUD-05 paso a paso: prototipo → pasabanda → bilineal vs invarianza (out/m8_iir_pasos.png)
```
`run_all` ejecuta los tres. Los parámetros (tonos, Fs, órdenes, umbrales) se **leen de `../dsp/dsp_config.h`**
(`dsp_params.m`), así que MATLAB y la Pico no se desincronizan. Los ruidos NoiseX-92 salen de `../host/noise/`.
En `demo_dsp.m` cambia `SNR_DB` o `NOISE_KIND` (0 AWGN, 1 AWGN + 2 NoiseX, 2 solo NoiseX) para experimentar.

## Demo en tiempo real (`live_demo`)

Cada 50 ms genera lo que “captaría” el micrófono: **tonos + AWGN + NoiseX-92 `factory1` + NoiseX-92 `volvo`** (las fuentes
que pide REQ-AUD-03, no audio externo), lo pasa por la cadena DSP y **reproduce** lo que elijas en *Escuchar*: tono limpio,
contaminada, salida FIR o salida IIR. Controles: tono (silencio / A 700 Hz / B 1000 Hz / A+B), SNR −15…+15 dB, qué fuentes de
ruido están activas, volumen y recalibrar. Los primeros ~3 s calibra con solo ruido; luego elige un tono.
Requiere Audio Toolbox (`audioDeviceWriter`); sin audio: `live_demo('NoAudio', true)`. **Empieza con el volumen bajo.**
`test_live.m` corre el motor sin pantalla ni audio con un guion (calibración → A → B → A+B → A a −12 dB).

## Mapa de archivos

| Archivo | Qué hace | Req. |
|---|---|---|
| `design_fir_multiband.m` | FIR por ventana Hamming y superposición de bandas ideales | 04 |
| `design_iir_bandpass.m` | Butterworth → pasabanda → bilineal con prewarping → biquads | 05 |
| `filters_ref.m` | **muestra a muestra**: bufer circular, forma directa I y II (como en C) | 06 |
| `filters_fast.m` | los mismos filtros vectorizados con `filter` (para barridos largos) | — |
| `goertzel_blocks.m` | Goertzel con ventana Hann; modo recursivo (`explicit`) o bin DFT | 07 |
| `detector_run.m` | umbral calibrado, ambigüedad, SNR, duración mínima, reposo | 08, 10 |
| `calibrate_thr.m`, `run_chains.m`, `design_all.m` | utilidades | — |
| `load_noisex.m`, `gen_noise.m`, `resample_sinc.m`, `quant12.m` | AWGN + NoiseX-92, ADC de 12 bits | 03 |
| `welch_psd.m` | PSD de Welch con `fft` | 09 |
| `LiveEngine.m`, `detector_step.m`, `live_demo.m` | motor por bloques + detector con estado + interfaz con audio | 03, 08 |
| `explain_order_M.m`, `explain_iir.m` | guías numéricas del orden M y de REQ-AUD-05 | 04, 05 |

## Qué se verificó (corrida real en MATLAB R2025a)

* **Mismo diseño que la Pico:** diferencia máxima en coeficientes 5e-12 (FIR) y 4e-10 (IIR); respuesta en frecuencia vs
  `host/out/response.csv` dentro de 0.002 dB.
* **Filtros a mano == `filter`:** diferencia ~1e-16 (bloqueo DC, FIR y banco IIR). Goertzel recursivo == bin DFT: ~1e-17.
* **Perfilado (tono 700 Hz, SNR 0 dB, AWGN + factory1 + volvo):** atenuación entre tonos FIR 56.5 dB / IIR 12.8 dB
  (igual que en C); ganancia de SNR +15.3 / +13.5 dB (C: +14.8 / +13.1; difiere el tramo de ruido y la semilla).
* **Escenario de 4 s a −3 dB:** las tres cadenas detectan campo 1 y luego campo 2, en orden.

* **Curva de estrés (100 pruebas por punto):** 100 % de +10 a −8 dB y 0 % a −15 dB, igual que en C. En el borde (−10/−12 dB) los
  porcentajes difieren de C (p. ej. AWGN+NoiseX a −10 dB: MATLAB 87/81/79 % vs C 48.5/72/41.5 % sin filtro/FIR/IIR): el umbral se
  calibra una sola vez por escenario y ahí manda esa calibración, así que **no hay evidencia de que un filtro detecte mejor que otro**.

Los generadores aleatorios de MATLAB y de C son distintos: fuera del borde los porcentajes coinciden, en el borde no.
