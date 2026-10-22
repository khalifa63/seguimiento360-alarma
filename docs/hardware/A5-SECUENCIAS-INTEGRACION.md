# DOCUMENTO DE ARQUITECTURA DE firmware: HUB-S3
## Sección A5: Secuencias de Integración v0.3

**Autor:** Arquitecto Jefe de Firmware
**Proyecto:** Seguimiento 360
**Estado:** v0.3 (Corregido: alineación §6 + LMK coherente con A4)
**Referencia:** SPEC-03, A1 v0.2, A2 v0.2, A3 v0.1, A4 v0.2

---

### 1. Resumen Ejecutivo y Contratos de Mensajería

Esta sección define los flujos de integración críticos entre el HUB-S3, los nodos ESP-NOW y los servicios backend (SRV-CORE-002/003).

*   **Topics MQTT (alineados con §6, multi-tenant):**
    `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/...`
    *   `.../status` (Retained, QoS1; incluye LWT offline)
    *   `.../event` (QoS1)
    *   `.../command` (sub, Retained, QoS1; payload con `action`: arm | disarm | ota | rotate_creds)
    *   `.../response` (pub QoS1)
    *   `.../hello` (pub)
    *   `.../pairing/req` y `.../pairing/resp` (PROVISIONALES hasta aprobar SRV-CORE-002; misma jerarquía)
*   **Frames ESP-NOW v1:** `PAIRING_REQ`, `PAIRING_RESP`, `DATA_EVENT`, `CTRL_CMD`, `ACK`.

---

### 2. Secuencia 1: Pairing de Nodo (Alta de Sensor)

**Participantes:** `Nodo`, `Hub-S3` (`t_espnow_rx`, `t_node_mgr`), `SRV-CORE-002`.
**Objetivo:** Vincular nodo al tenant y distribuir LMK.

~~text
[Nodo]                [Hub-S3 (t_espnow/t_node)]      [SRV-CORE-002]
  |                               |                               |
  |-- PAIRING_REQ (TempKey) ------>|                               |
  | (ClaimCode + MAC)             |-- MQTT PUBLISH -------------->|
  |                               |  topic: .../pairing/req       |
  |                               |  payload: {mac, claim_code}   |
  |                               |                               |
  |                               |<-- MQTT PUBLISH --------------|
  |                               |  topic: .../pairing/resp      |
  |                               |  payload: {node_id, policy}   |
  |                               |                               |
  |                               | (Hub genera LMK con HW RNG;   |
  |                               |  guarda en nvs_nodes)         |
  |<-- PAIRING_RESP (TempKey) -----|                               |
  | (Guarda LMK en NVS)           |                               |
  |                               |                               |
  |-- DATA_EVENT (LMK) ---------->| (Primer heartbeat/estado)     |
  |                               |-- MQTT PUBLISH -------------->|
  |                               |  topic: .../event             |
  |                               |  payload: {type:              |
  |                               |   node_registered, node_id}   |
~~

**Condiciones de Error y Timeouts:**
*   **Timeout Pairing:** Si `SRV-CORE-002` no responde en 10s, el Hub envía `PAIRING_RESP` con error al Nodo. El Nodo reintenta con backoff.
*   **Claim Inválido:** Si el servidor rechaza el `claim_code`, el Hub notifica al Nodo y aborta.
*   **Decisión de Diseño (v0.3, coherente con A4 v0.2):** El Hub GENERA la LMK localmente con el RNG de hardware. El servidor es fuente de verdad de IDENTIDAD (node_id, tenant, políticas), NO de criptografía: la LMK nunca sale del edge. Un compromiso del servidor no expone claves de nodos.

---

### 3. Secuencia 2: Evento de Sensor -> Servidor -> Reglas -> Sirena

**Participantes:** `Nodo (PIR)`, `Hub-S3` (`t_espnow_rx`, `t_logic`), `SRV-CORE-003`, `Nodo (Sirena)`.

~~text
[PIR]      [Hub-S3 (t_espnow_rx)]  [Hub-S3 (t_logic)]  [SRV-CORE-003]  [Sirena]
  |               |                       |                   |               |
  |-- DATA_EVENT ->|                       |                   |               |
  | (Seq, CRC)    |-- Validar Seq/CRC -->|                   |               |
  |               |                       |                   |               |
  |<-- ACK --------| (< 50 ms)            |                   |               |
  |               |-- Encolar xQ_Logic -->|                   |               |
  |               |                       |-- Evaluar SM_ALARM|               |
  |               |                       |  (Si ARMED -> TRIGGERED)          |
  |               |                       |-- MQTT PUBLISH -->|               |
  |               |                       |  topic: .../event |               |
  |               |                       |  payload: {pir_1} |               |
  |               |                       |                   |-- (Reglas Backend)
  |               |                       |                   |-- (Push Notif)
  |               |<-- xQ_NodeTx ---------|                   |               |
  |               |-- CTRL_CMD (SIRENA_ON) ---------------------------------->|
  |               | (LMK, Seq)            |                   |               |
~~

**Condiciones de Error y Timeouts:**
*   **Presupuesto RX->Publish:** desde el bit de stop del PIR hasta el `PUBLISH` en el socket TCP ≤ 250 ms.
*   **Fallo de Secuencia:** si `seq <= last_seq`, el Hub descarta el frame silenciosamente (sin ACK). El Nodo retransmitirá.
*   **Decisión de Diseño:** la sirena se activa por lógica LOCAL (SM_ALARM), no esperando al servidor. Disuasión inmediata aunque caiga internet; el servidor recibe el evento en paralelo para notificaciones.

---

### 4. Secuencia 3: Arm/Disarm vía PWA

**Participantes:** `PWA`, `SRV-CORE-003`, `Hub-S3` (`t_wifi_mqtt`, `t_logic`).

~~text
[PWA]                [SRV-CORE-003]               [Hub-S3 (t_wifi/t_logic)]
  |                       |                               |
  |-- POST /api/ --------->|                               |
  |  alarm/control         |                               |
  |                       |-- MQTT PUBLISH (Retained) ---->|
  |                       |  topic: .../command             |
  |                       |  payload: {action:"arm",        |
  |                       |   issued_by, issued_at,         |
  |                       |   request_id}                   |
  |                       |                               |-- Validar PIN/Token
  |                       |                               |-- SM_ALARM: ARMED
  |                       |                               |   (Inicia Exit Delay)
  |                       |<-- MQTT PUBLISH --------------|
  |                       |  topic: .../status              |
  |                       |  payload: {state: "arming"}     |
  |                       |<-- MQTT PUBLISH --------------|
  |                       |  topic: .../response            |
  |                       |  payload: {action, status}      |
  |<-- 200 OK -----------|                               |
  | (UI: "Armando...")    |                               |
~~

**Condiciones de Error y Timeouts:**
*   **Timeout de Comando:** si el Hub no recibe el comando en 5s, la PWA muestra error de red.
*   **PIN Inválido:** validación en servidor; el Hub publica `status: {error: "auth_fail"}` y no cambia de estado.
*   **Decisión de Diseño:** el `Exit Delay` se gestiona como timer interno en `t_logic` durante la transición `DISARMED` -> `ARMED`. Sin estado FSM intermedio (ahorro RAM).

---

### 5. Secuencia 4: LWT Offline (Corte de Energía)

**Participantes:** `Hub-S3` (`t_sys_mon`, `t_wifi_mqtt`), `Broker MQTT`, `SRV-CORE-003`.

~~text
[Hub-S3 (t_sys_mon)]    [Hub-S3 (t_wifi_mqtt)]    [Broker MQTT]      [SRV-CORE-003]
  |                             |                         |                   |
  |-- ADC detecta fallo --------|                         |                   |
  | (red eléctrica)             |-- MQTT PUBLISH (QoS1) ->|                   |
  |                             |  topic: .../event       |                   |
  |                             |  payload: {power_loss}  |                   |
  |                             |                         |-- Forward -------->|
  |                             |                         |                   |-- Alerta "Corte Luz"
  |-- BROWNOUT DETECT ----------|                         |                   |
  |                             X                         |                   |
  |                             | (Corte abrupto)         |                   |
  |                             |                         |-- MQTT PUBLISH --->|
  |                             |                         |  topic: .../status |
  |                             |                         |  payload: offline  |
  |                             |                         |  (LWT Message)     |
  |                             |                         |                   |-- Alerta "Hub Caído"
~~

**Condiciones de Error y Timeouts:**
*   **Flush de NVS:** al detectar `power_loss`, `t_sys_mon` fuerza commit de NVS (secuencias, estado alarma) antes del apagado. Si falla, se ignora para priorizar el envío MQTT.
*   **LWT vs Evento Explícito:** `power_loss` es la notificación "graceful"; el LWT `offline` es la confirmación "brutal" (corte de internet o muerte total). Cambian severidad y tipo de notificación.

---

### 6. Secuencia 5: OTA (Actualización Remota)

**Participantes:** `SRV-CORE-003`, `Hub-S3` (`t_wifi_mqtt`, tarea OTA).

~~text
[SRV-CORE-003]                 [Hub-S3 (t_wifi/t_ota)]
  |                                   |
  |-- MQTT PUBLISH (Retained) -------->|
  |  topic: .../command                |
  |  payload: {action:"ota",           |
  |   url, sha256, sig, ver}           |
  |                                   |-- 1. Validar firma del manifiesto
  |                                   |-- 2. HTTPS GET binario -> ota_1
  |                                   |-- 3. SHA256 en flash
  |                                   |-- (Si Hash/Sig != esperado) --> ABORT
  |                                   |-- 4. esp_ota_set_boot_partition(ota_1)
  |                                   |-- 5. esp_restart()
  | (Reboot en < 10s)                 |
  |                                   |-- Boot desde ota_1, init WiFi/MQTT
  |<-- MQTT PUBLISH ------------------|
  |  topic: .../status                 |
  |  payload: {state:"online",         |
  |            fw_ver:"x.y.z"}         |
  |                                   |-- (Si panic o WDT en 30s)
  |                                   |-- Bootloader revierte a ota_0
~~

**Condiciones de Error y Timeouts:**
*   **Presupuesto Boot:** desde `esp_restart()` hasta `status: online` ≤ 10 s.
*   **Fallo de Descarga:** aborta, borra `ota_1` parcial, reintenta con backoff.
*   **Rollback:** si el nuevo firmware no confirma boot en 30s, el WDT resetea y el bootloader arranca `ota_0`.

---

### 7. Registro de Decisiones de Diseño

1.  **Sirena Local vs Servidor:** el Hub evalúa SM_ALARM localmente. La latencia de red (>500ms ida/vuelta) es inaceptable para disuasión física. El servidor solo procesa notificaciones.
2.  **LWT y Power Loss:** evento `power_loss` graceful + LWT brutal. Permite al backend diferenciar "corte de luz" de "fallo de hardware/internet".
3.  **OTA en Dos Pasos:** validar firma del manifiesto antes de descargar el binario (ahorra ancho de banda y CPU).
4.  **(v0.3) Topics alineados con §6:** prefijo completo `seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/`, un solo topic `command` con campo `action` (arm/disarm/ota/rotate_creds), coherente con el servidor implementado en Fases 1-3.2.3. Los topics `pairing/*` son provisionales hasta aprobar SRV-CORE-002.
5.  **(v0.3) LMK generada en el Hub:** coherente con A4 v0.2. El servidor devuelve `node_id` y políticas, nunca la LMK. Justificación: zero-trust; un compromiso del backend no expone claves de nodos.

---
**Fin de la Sección A5 v0.3**
