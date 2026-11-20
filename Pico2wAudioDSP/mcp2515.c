#include <stdio.h>
#include "mcp2515.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

static inline void cs_select(void)   { gpio_put(MCP2515_PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(MCP2515_PIN_CS, 1); }

void mcp2515_init(void)
{
    /* Keep the Zumo RGB LED clock inactive. */
    gpio_init(6);
    gpio_put(6, 0);
    gpio_set_dir(6, GPIO_OUT);
    /* Establish CS high before starting SPI. */
    gpio_init(MCP2515_PIN_CS);
    gpio_put(MCP2515_PIN_CS, 1);
    gpio_set_dir(MCP2515_PIN_CS, GPIO_OUT);

    spi_init(MCP2515_SPI_PORT, MCP2515_SPI_BAUD);
    spi_set_format(MCP2515_SPI_PORT, 8,
                   SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MCP2515_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MCP2515_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(MCP2515_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(MCP2515_PIN_INT);
    gpio_set_dir(MCP2515_PIN_INT, GPIO_IN);
    gpio_pull_up(MCP2515_PIN_INT);
    mcp2515_reset();
}

void mcp2515_reset(void) {
    uint8_t cmd = MCP2515_CMD_RESET;
    cs_select();
    spi_write_blocking(MCP2515_SPI_PORT, &cmd, 1);
    cs_deselect();
    sleep_ms(10);
}

uint8_t mcp2515_read_register(uint8_t address) {
    uint8_t tx[3] = { MCP2515_CMD_READ, address, 0x00 };
    uint8_t rx[3];
    cs_select();
    spi_write_read_blocking(MCP2515_SPI_PORT, tx, rx, 3);
    cs_deselect();
    return rx[2];
}


void mcp2515_write_register(uint8_t address, uint8_t value) {
    uint8_t tx[3] = { MCP2515_CMD_WRITE, address, value };
    cs_select();
    spi_write_blocking(MCP2515_SPI_PORT, tx, 3);
    cs_deselect();
}

void mcp2515_modify_register(uint8_t address, uint8_t mask, uint8_t value) {
    uint8_t tx[4] = { MCP2515_CMD_BIT_MODIFY, address, mask, value };
    cs_select();
    spi_write_blocking(MCP2515_SPI_PORT, tx, 4);
    cs_deselect();
}

uint8_t mcp2515_read_status(void) {
    uint8_t tx[2] = { MCP2515_CMD_READ_STATUS, 0x00 };
    uint8_t rx[2];
    cs_select();
    spi_write_read_blocking(MCP2515_SPI_PORT, tx, rx, 2);
    cs_deselect();
    return rx[1];
}

/* SPI link test: verifies MOSI, MISO, SCK and CS by writing patterns to a
 * config register (CNF1) and reading them back. Returns true if all pass. */
bool mcp2515_spi_selftest(void) {
    bool ok = true;
    mcp2515_reset();

    uint8_t canstat = mcp2515_read_register(MCP2515_CANSTAT);
    uint8_t canctrl = mcp2515_read_register(MCP2515_CANCTRL);
    printf("[SPI] after reset: CANSTAT=0x%02X (expect 0x80)  CANCTRL=0x%02X (expect 0x87)\n",
           canstat, canctrl);
    if ((canstat & MCP2515_MODE_MASK) != MCP2515_MODE_CONFIG) ok = false;

    static const uint8_t patterns[] = { 0x55, 0xAA, 0x00, 0xFF, 0x0F, 0xF0, 0x12 };
    for (unsigned i = 0; i < sizeof patterns; i++) {
        mcp2515_write_register(MCP2515_CNF1, patterns[i]);
        uint8_t back = mcp2515_read_register(MCP2515_CNF1);
        printf("[SPI] CNF1 write 0x%02X -> read 0x%02X %s\n",
               patterns[i], back, back == patterns[i] ? "ok" : "MISMATCH");
        if (back != patterns[i]) ok = false;
    }
    mcp2515_write_register(MCP2515_CNF1, 0x00);

    printf("[SPI] INT idle level = %d (expect 1)\n", gpio_get(MCP2515_PIN_INT));
    if (!gpio_get(MCP2515_PIN_INT)) ok = false;

    if (!ok) {
        printf("[SPI] FAIL. Hints: all reads 0x00 -> MISO stuck low or chip unpowered;\n"
               "      all reads 0xFF -> MISO floating/high or CS not reaching chip;\n"
               "      CANSTAT ok but CNF1 mismatch -> MOSI or SCK problem.\n");
    } else {
        printf("[SPI] PASS\n");
    }
    return ok;
}

bool mcp2515_set_mode(uint8_t mode)
{
    mcp2515_modify_register(MCP2515_CANCTRL,
                           MCP2515_MODE_MASK, mode);

    absolute_time_t deadline = make_timeout_time_ms(100);
    do {
        uint8_t status = mcp2515_read_register(MCP2515_CANSTAT);
        if ((status & MCP2515_MODE_MASK) == mode) {
            return true;
        }
        sleep_ms(1);
    } while (!time_reached(deadline));
    return false;
}
bool mcp2515_set_bitrate_500kbps_8mhz(void) {
    if (!mcp2515_set_mode(MCP2515_MODE_CONFIG)) {
    return false;
    }
    mcp2515_write_register(MCP2515_CNF1, 0x00);
    mcp2515_write_register(MCP2515_CNF2, 0x90);
    mcp2515_write_register(MCP2515_CNF3, 0x02);
    mcp2515_write_register(MCP2515_CANINTE, MCP2515_CANINTF_RX0IF);
    mcp2515_write_register(MCP2515_RXB0CTRL, 0x60); // accept all messages, no filters/masks
    return true;
}
bool mcp2515_send_message(const can_frame_t *frame) {
    if (frame->dlc > 8) return false;

    uint8_t sidh = (uint8_t)(frame->id >> 3);
    uint8_t sidl = (uint8_t)((frame->id & 0x07) << 5);
    mcp2515_write_register(MCP2515_TXB0SIDH, sidh);
    mcp2515_write_register(MCP2515_TXB0SIDL, sidl);
    mcp2515_write_register(MCP2515_TXB0DLC, frame->dlc);

    for (uint8_t i = 0; i < frame->dlc; i++) {
        mcp2515_write_register(MCP2515_TXB0D0 + i, frame->data[i]);
    }
    uint8_t cmd = MCP2515_CMD_RTS_TXB0;   // request-to-send TXB0
    cs_select();
    spi_write_blocking(MCP2515_SPI_PORT, &cmd, 1);
    cs_deselect();

    /* Wait for TXREQ (0x08) to clear. With no other node ACKing, the chip
     * retries forever, so TXREQ never clears and we must report failure. */
    bool sent = false;
    absolute_time_t deadline = make_timeout_time_ms(50);
    do {
        uint8_t ctrl = mcp2515_read_register(MCP2515_TXB0CTRL);
        if (!(ctrl & 0x08)) {                      // TXREQ cleared
            sent = !(ctrl & 0x30);                 // no TXERR / MLOA
            break;
        }
        sleep_ms(1);
    } while (!time_reached(deadline));

    if (!sent) {
        /* Abort the pending frame so it does not linger and fire later. */
        mcp2515_modify_register(MCP2515_TXB0CTRL, 0x08, 0x00);
    }
    mcp2515_modify_register(MCP2515_CANINTF, MCP2515_CANINTF_TX0IF, 0x00);
    return sent;
}

/* Prints TEC/REC/EFLG. Rising TEC with no REC = nobody ACKs (bus open,
 * termination, bitrate/crystal). TEC>=128 error-passive, EFLG bit5 bus-off. */
void mcp2515_print_bus_health(void) {
    uint8_t tec  = mcp2515_read_register(MCP2515_TEC);
    uint8_t rec  = mcp2515_read_register(MCP2515_REC);
    uint8_t eflg = mcp2515_read_register(MCP2515_EFLG);
    printf("[BUS] TEC=%u REC=%u EFLG=0x%02X%s%s\n", tec, rec, eflg,
           (eflg & 0x20) ? " BUS-OFF" : "",
           (tec >= 128 || rec >= 128) ? " ERROR-PASSIVE" : "");
}

bool mcp2515_message_available(void) {
    return (mcp2515_read_register(MCP2515_CANINTF) & MCP2515_CANINTF_RX0IF) != 0;
}
bool mcp2515_read_message(can_frame_t *frame) {
    if (!mcp2515_message_available()) return false;
    uint8_t sidh = mcp2515_read_register(MCP2515_RXB0SIDH);
    uint8_t sidl = mcp2515_read_register(MCP2515_RXB0SIDL);
    frame->id = ((uint32_t)sidh << 3) | (sidl >> 5);

    frame->dlc = mcp2515_read_register(MCP2515_RXB0DLC) & 0x0F;
    for (uint8_t i = 0; i < frame->dlc; i++) {
        frame->data[i] = mcp2515_read_register(MCP2515_RXB0D0 + i);
    }
    mcp2515_modify_register(MCP2515_CANINTF, MCP2515_CANINTF_RX0IF, 0x00);
    return true;
}