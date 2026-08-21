/*
 * sm_conn.h — FSM de conectividad WiFi STA + arranque MQTT
 * [HUB-F-020] estados DISCONNECTED/CONNECTING/CONNECTED/ERROR_BACKOFF
 * [HUB-NF-004] backoff 1→2→4→8→16→32→60s + jitter ±20%  [R2 A6]
 * [HUB-NF-003] canal fijo compartido con ESP-NOW  [R1 A6]
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SM_CONN_STATE_DISCONNECTED,
    SM_CONN_STATE_CONNECTING,
    SM_CONN_STATE_CONNECTED,
    SM_CONN_STATE_ERROR_BACKOFF,
} sm_conn_state_t;

typedef void (*sm_conn_state_cb_t)(sm_conn_state_t new_state, void *ctx);

typedef struct {
    const char *wifi_ssid;
    const char *wifi_pass;      /* NULL o "" => red abierta */
    uint8_t     channel;        /* [R1 A6] canal fijo compartido con ESP-NOW */
    sm_conn_state_cb_t on_state;
    void              *on_state_ctx;
} sm_conn_cfg_t;

esp_err_t sm_conn_init(const sm_conn_cfg_t *cfg);
esp_err_t sm_conn_start(void);
esp_err_t sm_conn_stop(void);
esp_err_t sm_conn_force_disconnect(void);   /* pruebas / watchdog */
sm_conn_state_t sm_conn_get_state(void);

#ifdef __cplusplus
}
#endif
