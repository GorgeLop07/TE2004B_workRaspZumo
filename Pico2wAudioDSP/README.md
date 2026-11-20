# Pico2wAudioDSP — Módulo DSP (REQ-AUD-01…10)

Detecta tonos de comando con el micrófono MAX4466 en la **Pico 2 W**, los acondiciona con un
**FIR multibanda** y un **banco IIR** (ambos diseñados a mano), valida con **Goertzel** y manda el
campo elegido al Zumo por CAN (`ID 0x102`) usando el mismo MCP2515 que ya probaron.

```
Pico2wAudioDSP/
├── main.c, audio_input.c   firmware: ADC+DMA a 8 kHz, bucle de detección, CAN, comandos serial
├── dsp/                    núcleo DSP (C11, float): lo comparten la Pico y la PC
│   ├── dsp_config.h          tonos, Fs, órdenes y umbrales: TODO se cambia aquí
│   ├── dsp_design.hpp        diseño FIR/IIR desde las ecuaciones (sin cajas negras)
│   ├── dsp_coeffs.h          GENERADO: coeficientes que se compilan en la Pico
│   └── dsp_runtime.c         bufer circular, FIR, IIR, Goertzel, validación
├── host/                   banco de pruebas en la PC (C++): AWGN + NoiseX-92, FFT, estrés, benchmark
├── matlab/                 la misma cadena en MATLAB, paso a paso, para entender y experimentar (ver matlab/README.md)
├── docs/MEMORIA_DE_CALCULO.md   entregable 1 con figuras y resultados
└── mcp2515.c/h             driver CAN (pines Pico 2 W: GP4/5/6/7, INT GP20)
```

## Tonos y mapeo (REQ-AUD-01)

| Campo | Tono | Bin Goertzel (N=400) |
|---|---|---|
| 1 (A) | 700 Hz | 35 |
| 2 (B) | 1000 Hz | 50 |

Separación 300 Hz (≥ 150 Hz), ambos > 100 Hz. Cambiar en `dsp/dsp_config.h` y correr `make -C host gen`.

## Probar en la PC (sin hardware)

```bash
cd host
make gen            # diseña los filtros -> dsp/dsp_coeffs.h + out/design_report.txt
make run            # banco de pruebas completo (~3 min) y figuras en host/out/
```
Necesita los WAV de NoiseX-92 en `host/noise/` (`factory1.wav`, `volvo.wav`, de
`github.com/speechdnn/Noises/tree/master/NoiseX-92`).

## Probar en la Pico 2 W

1. Compilar con la extensión de VS Code (abrir esta carpeta) y flashear el `.uf2`. Abrir el monitor serial.
2. Debe verse `[SPI] PASS`, `CAN listo`, y cada segundo una línea `[AUD] fs=8000.x Hz CPU=… DC=… rms=…`.
   - `fs` debe ser ≈ 8000 (REQ-AUD-02), `DC` ≈ 2048 con el micrófono en silencio, `overruns=0`.
3. Dejar el ambiente **sin tonos** ~3 s: imprime `[CAL] listo` con los umbrales.
4. Reproducir el tono de 700 o 1000 Hz (celular/bocina) cerca del micrófono. Aparece
   `*** COMANDO campo N … -> CAN 0x102 enviado`.
5. Comandos serial: `b` benchmark (Entregable 4), `t` autoprueba AWGN en la placa, `c` recalibrar,
   `r`/`f`/`i` decidir sin filtro/FIR/IIR, `v` ver cada bloque.

### ⚠ El pin GP29
En la Pico 2 W **GP29 no sale al header**: es ADC3 y mide VSYS/3 (comparte línea con el reloj del
WiFi). Los pines ADC del header son GP26/27/28. Si el micrófono está soldado a GP29 y no se ve señal,
mover el cable a **GP26** y poner `#define MIC_GPIO 26` en `main.c`.

### Integración con el robot
El mensaje está en `audio_can.h` (`0x102`, DLC 4: campo, SNR, amplitud, seq). Este proyecto es
independiente de `Pico2wLocalPositioningCAN` (que usa WiFi y el chip CYW43, y ya manda `0x101` cada 50 ms):
para correr los dos en la misma placa hay que llamar a `audio_input_*` y al detector desde ese
bucle, y el Zumo debe decodificar `0x102` (todavía no lo hace).
