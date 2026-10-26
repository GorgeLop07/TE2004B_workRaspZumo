# Challenge: Local Positioning (WiFi + CAN)

| Carpeta | Placa | Parte del PDF |
|---|---|---|
| `LocalPositioning/` | Pico 2 W | Parte 1 (código del PDF tal cual, imprime el JSON) |
| `Pico2wLocalPositioningCAN/` | Pico 2 W | Parte 2, punto 2 (WiFi → CAN) |
| `ZumoLocalPositioningCAN/` | Zumo 2040 (pico) | Parte 2, punto 3 (CAN → motores) |

Cada carpeta es un proyecto independiente de la extensión Raspberry Pi Pico:
en VS Code `File > Open Folder...` sobre la carpeta del proyecto, luego **Compile Project** y **Run Project**
(o arrastrar el `.uf2` de `build/` en modo BOOTSEL).

## Antes de probar

En `Pico2wLocalPositioningCAN/main.c`:
- `TEAM_MARKER_ID`: el id de su marcador aruco (el que aparece en el JSON como `"id"`).
- `WIFI_SSID` / `WIFI_PASSWORD`: ya vienen como `embedded` / `12345678`.

En `ZumoLocalPositioningCAN/main.c` (calibración en pista):
- `FAST_SPEED`, `SLOW_SPEED`, `SLOW_ZONE_MM`: velocidad lejos y cerca del destino.
- `STOP_EARLY_MM`: si se pasa de los 50 cm, súbanlo con lo que se pasó (latencia cámara + CAN).
- `HEADING_KP`: corrección de rumbo con el ángulo de la cámara. En `0` va sin corrección.
  Si lo activan (p. ej. `3.0f`) y el carrito corrige al lado contrario, pónganlo negativo.

## CAN-ID database (Parte 2, 2c)

Bitrate 500 kbps, cristal de 8 MHz en el MCP2515, ID estándar de 11 bits, Pico 2 W → Zumo cada 50 ms.

| ID | Nombre | DLC | Bytes |
|---|---|---|---|
| `0x101` | LP_DATA | 8 | D0-D1 `x` [mm] int16, D2-D3 `y` [mm] int16, D4-D5 `angle` [0.1°] int16, D6 `status`, D7 `seq` |

Big-endian. `status`: bit0 = marcador visto al menos una vez, bit1 = dato fresco (< 500 ms).
`seq` aumenta con cada dato nuevo de la cámara.

Ejemplo: `x=0.341, y=-0.473, angle=-93.0` → `ID=0x101 DLC=8 Data=01 55 FE 27 FC 5E 03 07`.

## Secuencia de prueba

1. Flashear `Pico2wLocalPositioningCAN` en la Pico 2 W y abrir el monitor serial: debe aparecer
   `MCP2515 in NORMAL mode`, `Connected`, las líneas JSON y las líneas `TX ID=0x101 ...`.
2. Flashear `ZumoLocalPositioningCAN` en el Zumo. En el monitor serial aparece
   `WAIT_START | x=... y=...` cada 500 ms cuando ya le llegan tramas.
3. Colocar el Zumo en el punto de inicio mirando hacia el eje y. Presionar **A**.
4. El Zumo avanza imprimiendo `RX ID=0x101 ... | dy=xxx/500 mm | MOVING`, frena en los
   últimos `SLOW_ZONE_MM` y se detiene con `DESTINATION REACHED`.
5. Presionar **A** otra vez para repetir desde la posición actual.

Si se pierde el marcador o deja de llegar CAN (> 300 ms) el Zumo se detiene y continúa cuando regresa el dato.
El avance se mide como `|y - y_inicio|`, así que funciona aunque el carrito vaya hacia +y o -y.

## Notas

- Pines: Zumo GP0/1/2/3/28 y Pico 2 W GP16/17/18/19/20 en `spi0`, igual que la actividad de CAN.
- Se usa el `mcp2515.c` de la Parte 3 de la actividad de CAN en ambas placas. El prototipo de
  `mcp2515_set_mode` en el `.h` se dejó como `bool` porque en el PDF no coincidía con el `.c`.
- Si su módulo MCP2515 trae cristal de 16 MHz, cambiar `CNF1=0x00, CNF2=0xF0, CNF3=0x86`
  (comentario original del PDF) en ambos `mcp2515.c`.
