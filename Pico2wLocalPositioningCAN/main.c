#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "mcp2515.h"
#include "lp_can.h"

// Replace these with the network you want the Pico W to join.
static const char *WIFI_SSID = "embedded";
static const char *WIFI_PASSWORD = "12345678";
static const uint16_t TELEMETRY_PORT = 5000;

#define TEAM_MARKER_ID   1     // id of the team's aruco marker
#define CAN_PERIOD_MS    50
#define LP_TIMEOUT_MS    500
#define LP_LINE_MAX      512

static lp_data_t lp_latest;
static bool lp_received = false;
static absolute_time_t lp_last_update;

static bool json_get_number(const char *obj, const char *key, float *out) {
    const char *p = strstr(obj, key);
    if (p == NULL) return false;
    p = strchr(p + strlen(key), ':');
    if (p == NULL) return false;
    char *end;
    float value = strtof(p + 1, &end);
    if (end == p + 1) return false;
    *out = value;
    return true;
}

/* Looks for the team's marker inside the "markers" array of a telemetry line. */
static bool parse_team_marker(const char *line, float *x, float *y, float *angle) {
    const char *p = strstr(line, "\"markers\"");
    if (p == NULL) return false;
    while ((p = strchr(p, '{')) != NULL) {
        const char *end = strchr(p, '}');
        if (end == NULL) return false;
        char obj[128];
        size_t n = (size_t)(end - p);
        if (n >= sizeof(obj)) n = sizeof(obj) - 1;
        memcpy(obj, p, n);
        obj[n] = '\0';
        float id;
        if (json_get_number(obj, "\"id\"", &id) && lroundf(id) == TEAM_MARKER_ID) {
            return json_get_number(obj, "\"x\"", x) &&
                   json_get_number(obj, "\"y\"", y) &&
                   json_get_number(obj, "\"angle\"", angle);
        }
        p = end + 1;
    }
    return false;
}

static int16_t to_int16(float value) {
    long v = lroundf(value);
    if (v > INT16_MAX) v = INT16_MAX;
    if (v < INT16_MIN) v = INT16_MIN;
    return (int16_t)v;
}

// Assumes each telemetry line fits in a single pbuf (true for the JSON
// lines this listens for); a larger/fragmented UDP payload would need
// pbuf_copy_partial() over the whole pbuf chain instead of one segment.
static void on_udp_packet(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;
    if (p != NULL) {
        printf("%.*s\n", p->len, (char *)p->payload);

        char line[LP_LINE_MAX];
        size_t n = p->len < sizeof(line) - 1 ? p->len : sizeof(line) - 1;
        memcpy(line, p->payload, n);
        line[n] = '\0';

        float x, y, angle;
        if (parse_team_marker(line, &x, &y, &angle)) {
            lp_latest.x_mm = to_int16(x * 1000.0f);
            lp_latest.y_mm = to_int16(y * 1000.0f);
            lp_latest.angle_ddeg = to_int16(angle * 10.0f);
            lp_latest.seq++;
            lp_received = true;
            lp_last_update = get_absolute_time();
        }
        pbuf_free(p);
    }
}

static void send_lp_frame(void) {
    lp_data_t lp = lp_latest;
    lp.status = 0;
    if (lp_received) {
        lp.status |= LP_STATUS_VALID;
        if (absolute_time_diff_us(lp_last_update, get_absolute_time()) < LP_TIMEOUT_MS * 1000) {
            lp.status |= LP_STATUS_FRESH;
        }
    }

    can_frame_t frame;
    lp_pack(&lp, &frame);
    if (mcp2515_send_message(&frame)) {
        printf("TX  ID=0x%03X  DLC=%d  Data=", (unsigned)frame.id, frame.dlc);
        for (int i = 0; i < frame.dlc; i++) printf("%02X ", frame.data[i]);
        printf("| x=%d mm y=%d mm angle=%.1f deg status=0x%02X seq=%u\n",
               lp.x_mm, lp.y_mm, lp.angle_ddeg / 10.0f, lp.status, lp.seq);
    } else {
        printf("TX failed\n");
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000); // give the USB serial connection time to enumerate

    printf("\n--- Pico 2 W Local Positioning -> CAN ---\n");
    mcp2515_init();
    uint8_t canstat = mcp2515_read_register(MCP2515_CANSTAT);
    printf("MCP2515 CANSTAT=0x%02X after reset (0x80 = SPI OK)\n", canstat);
    if (!mcp2515_set_bitrate_500kbps_8mhz()) {
        printf("MCP2515 config failed. Check wiring.\n");
    }
    if (mcp2515_set_mode(MCP2515_MODE_NORMAL)) {
        printf("MCP2515 in NORMAL mode\n");
    } else {
        printf("MCP2515 failed to enter NORMAL mode\n");
    }

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                              CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Wi-Fi connect failed, retrying...\n");
    }
    printf("Connected\n");

    // Equivalent of Arduino's WiFi.noLowPowerMode(): without this, the
    // radio buffers broadcast packets until periodic wake-ups instead of
    // receiving them promptly, causing multi-second delays.
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    struct udp_pcb *pcb = udp_new();
    udp_bind(pcb, IP_ADDR_ANY, TELEMETRY_PORT);
    udp_recv(pcb, on_udp_packet, NULL);

    absolute_time_t next_send = make_timeout_time_ms(CAN_PERIOD_MS);
    while (true) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1));

        if (time_reached(next_send)) {
            next_send = make_timeout_time_ms(CAN_PERIOD_MS);
            send_lp_frame();
        }
    }
}
