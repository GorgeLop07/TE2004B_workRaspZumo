#include "encoders.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "quadrature_encoder.pio.h"

#define ENCODER_PIO pio0
#define LEFT_ENCODER_PIN 12
#define RIGHT_ENCODER_PIN 8

static uint encoder_offset;
static uint left_sm;
static uint right_sm;
static bool ready;

static void encoder_sm_init(uint sm, uint pin)
{
    pio_sm_set_consecutive_pindirs(ENCODER_PIO, sm, pin, 2, false);
    pio_gpio_init(ENCODER_PIO, pin);
    pio_gpio_init(ENCODER_PIO, pin + 1);
    gpio_pull_up(pin);
    gpio_pull_up(pin + 1);

    pio_sm_config config = quadrature_encoder_program_get_default_config(encoder_offset);
    sm_config_set_in_pins(&config, pin);
    sm_config_set_jmp_pin(&config, pin);
    sm_config_set_in_shift(&config, false, false, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&config, 1.0f);
    pio_sm_init(ENCODER_PIO, sm, encoder_offset, &config);
    pio_sm_set_enabled(ENCODER_PIO, sm, true);
}

static int32_t encoder_sm_read(uint sm)
{
    uint32_t value = 0;
    int pending = (int)pio_sm_get_rx_fifo_level(ENCODER_PIO, sm) + 1;
    while (pending-- > 0) {
        value = pio_sm_get_blocking(ENCODER_PIO, sm);
    }
    /* Pololu's encoder phase order is opposite the PIO decoder's sign. */
    return -(int32_t)value;
}

bool encoders_init(void)
{
    if (!pio_can_add_program(ENCODER_PIO, &quadrature_encoder_program)) {
        ready = false;
        return false;
    }

    encoder_offset = pio_add_program(ENCODER_PIO, &quadrature_encoder_program);
    if (encoder_offset != 0) {
        pio_remove_program(ENCODER_PIO, &quadrature_encoder_program, encoder_offset);
        ready = false;
        return false;
    }

    left_sm = pio_claim_unused_sm(ENCODER_PIO, false);
    right_sm = pio_claim_unused_sm(ENCODER_PIO, false);
    if (left_sm == (uint)-1 || right_sm == (uint)-1) {
        if (left_sm != (uint)-1) pio_sm_unclaim(ENCODER_PIO, left_sm);
        if (right_sm != (uint)-1) pio_sm_unclaim(ENCODER_PIO, right_sm);
        pio_remove_program(ENCODER_PIO, &quadrature_encoder_program, encoder_offset);
        ready = false;
        return false;
    }

    encoder_sm_init(left_sm, LEFT_ENCODER_PIN);
    encoder_sm_init(right_sm, RIGHT_ENCODER_PIN);
    ready = true;
    return true;
}

bool encoders_read(int32_t *left_count, int32_t *right_count)
{
    if (!ready || left_count == NULL || right_count == NULL) return false;
    *left_count = encoder_sm_read(left_sm);
    *right_count = encoder_sm_read(right_sm);
    return true;
}
