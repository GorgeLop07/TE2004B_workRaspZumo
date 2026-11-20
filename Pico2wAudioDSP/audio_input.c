#include "audio_input.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#define RING_BYTES (AUDIO_RING_SAMPLES * 2)
#define RING_LOG2  12                    /* 4096 B = 2^12; el bufer debe estar alineado a su tamano */
#if (1u << RING_LOG2) != RING_BYTES
#error "AUDIO_RING_SAMPLES debe ser 2048 (RING_LOG2 = 12)"
#endif

static uint16_t ring[AUDIO_RING_SAMPLES] __attribute__((aligned(RING_BYTES)));
static int dma_ch = -1;
static uint32_t rd = 0, overruns = 0;

static inline uint32_t wr_index(void) {
    return (dma_channel_hw_addr(dma_ch)->write_addr - (uint32_t)(uintptr_t)ring) >> 1;
}

void audio_input_init(unsigned gpio, unsigned fs_hz) {
    adc_init();
    adc_gpio_init(gpio);
    adc_select_input(gpio - ADC_BASE_PIN);
    adc_fifo_setup(true, true, 1, false, false);       /* FIFO + DREQ, 12 bits sin recortar */
    /* Periodo de muestra = (1 + div) / 48 MHz  ->  div = 48e6/Fs - 1  (5999 para 8 kHz, exacto) */
    adc_set_clkdiv(48000000.0f / (float)fs_hz - 1.0f);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);
    channel_config_set_ring(&c, true, RING_LOG2);       /* envuelve la direccion de escritura */
    dma_channel_configure(dma_ch, &c, ring, &adc_hw->fifo, dma_encode_endless_transfer_count(), true);
    rd = 0;
    adc_run(true);
}

uint32_t audio_input_backlog(void) { return (wr_index() - rd) & (AUDIO_RING_SAMPLES - 1); }

uint32_t audio_input_pull(uint16_t *dst, uint32_t max) {
    uint32_t avail = audio_input_backlog();
    if (avail > AUDIO_RING_SAMPLES / 2) overruns++;     /* si llega a una vuelta completa se perderian muestras sin aviso */
    if (avail > max) avail = max;
    for (uint32_t i = 0; i < avail; i++) { dst[i] = ring[rd]; rd = (rd + 1) & (AUDIO_RING_SAMPLES - 1); }
    return avail;
}

void audio_input_flush(void) { rd = wr_index(); }
uint32_t audio_input_overruns(void) { return overruns; }
