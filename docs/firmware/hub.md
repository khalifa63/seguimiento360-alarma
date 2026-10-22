# HUB-S3 Firmware — Hito M2: ESP-NOW + MQTT WSS

Estado: M2 listo para validación en hardware.
Specs: A1 v0.2, A2 v0.2, A5 v0.3, A6 v0.2 (R1/R2), INFRA-REAL v1.0.
Base: M1 (commit be38fb3).

## Arquitectura M2

```text
[Nodo ESP32] --ESP-NOW--> espnow_link (CRC16, anti-replay, ACK inline <50ms)
                                 |--> xQ_EspnowLogic --> t_espnow_rx --> JSON
                                                                          |
                                                                   xQ_MqttOut
                                                                          v
[WiFi STA] <-- sm_conn (FSM + backoff + jitter) --> mqtt_link (WSS) --> broker
                                                                          |
                                        t_wifi_mqtt <-- xQ_MqttCmd <-- command
```

Tareas: t_sys_mon (C0,P1) | t_espnow_rx (C0,P24) | t_wifi_mqtt (C1,P15) |
sm_conn (C1,P10).

## Config dev (temporal, M4 lo elimina)

Editar en main/main.c: DEV_WIFI_SSID / DEV_WIFI_PASS.
M2_FIXED_CHANNEL=0 (auto). Producción: canal fijo del AP [R1 A6].

## Build & Flash

```bash
. $IDF_PATH/export.sh
cd firmware/hub
rm -f sdkconfig && rm -rf build
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Validación

### AT-01 — boot -> online <= 10s
Monitor: medir desde "=== HUB-S3 M2 boot ===" hasta "[AT-01] MQTT online".
En el servidor (broker interno Docker):
```bash
docker exec mosquitto mosquitto_sub -h mosquitto -t "seguimiento360/#" -v
```
Esperado: .../status con {"state":"online","fw":"0.2.0",...} (retained).

### AT-07 — WiFi -> MQTT < 10s
Logs: "got ip: ..." -> "MQTT connected" en < 10s (típico 1-3s con TLS).

### AT-02 — ACK ESP-NOW < 50ms
PENDIENTE: simulador de nodo ESP-NOW (se entrega en M2bis).

### Regresión M1
- GPIO2 -> GND: log "TAMPER type=0 (M3: SM_ALARM)"
- LED heartbeat cada 5s
- Device ID persiste tras reset (botón EN)

## Topics (contratos §6)

Base: seguimiento360/tenant/{tid}/site/{sid}/hub/{mac}/

| Sub      | Dir | QoS | Retain |
|----------|-----|-----|--------|
| status   | pub | 1   | 1      |
| event    | pub | 1   | 0      |
| response | pub | 1   | 0      |
| hello    | pub | 1   | 0      |
| command  | sub | 1   | 1      |

LWT: status con {"state":"offline","reason":"lwt"} retain=1.

## Troubleshooting

- TLS handshake fail: verificar CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y y
  acceso DNS a mqtt.seguimiento360.com.
- WiFi no asocia: revisar DEV_WIFI_SSID/PASS; authmode WPA2.
- Puerto ocupado: pkill -f idf_monitor | salir del monitor con Ctrl+].

## Trazabilidad

| ID | Descripción | Archivo |
|----|-------------|---------|
| HUB-F-006 | RX + ACK <50ms | espnow_link.c |
| HUB-F-007 | CRC16 + anti-replay | espnow_link.c |
| HUB-F-010 | Publish QoS1 | mqtt_link.c |
| HUB-F-011 | Sub command retained | mqtt_link.c |
| HUB-F-012 | LWT offline | mqtt_link.c |
| HUB-F-013 | Auth NVS/anónimo | mqtt_link.c |
| HUB-F-020 | FSM conectividad | sm_conn.c |
| HUB-NF-003 | Canal fijo | sm_conn.c |
| HUB-NF-004 | Backoff + jitter | sm_conn.c |
| AT-01/07 | Conectividad | main.c |
