{{# DOCUMENTO DE ARQUITECTURA DE firmware: HUB-S3
## Sección A5: Secuencias de Integración v0.2

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.2 (Corregido)  
**Referencia:** SPEC-03, A1 v0.2, A2 v0.2, A3 v0.1, A4 v0.2

---

### 1. Resumen Ejecutivo y Contratos de Mensajería

Esta sección define los flujos de integración críticos entre el HUB-S3, los nodos ESP-NOW y los servicios backend (SRV-CORE-002/003). Para garantizar la claridad, se establecen los siguientes contratos de direccionamiento:

*   **Topics MQTT (Multi-tenant):** `tenant/{tenant_id}/hub/{hub_mac}/...`
    *   `.../status` (Retained, QoS1)
    *   `.../event` (QoS1)
    *   `.../cmd/...` (Comandos downlink, QoS1)
*   **Frames ESP-NOW v1:** `PAIRING_REQ`, `PAIRING_RESP`, `DATA_EVENT`, `CTRL_CMD`, `ACK`.

---

### 2. Secuencia 1: Pairing de Nodo (Alta de Sensor)

**Participantes:** `Nodo`, `Hub-S3` (`t_espnow_rx`, `t_node_mgr`), `SRV-CORE-002`.
**Objetivo:** Distribuir LMK y vincular nodo al tenant.

```text
[Nodo]                [Hub-S3 (t_espnow/t_node)]      [SRV-CORE-002]
  |                               |                               |
  |-- PAIRING_REQ (TempKey) ------>|                               |
  | (ClaimCode + MAC)             |-- MQTT PUBLISH -------------->|
  |                               |  topic: .../pairing/req       |
  |                               |  payload: {mac, claim_code}   |
  |                               |                               |
  |                               |<-- MQTT SUBSCRIBE ------------|
  |                               |  topic: .../pairing/resp      |
  |                               |  payload: {lmk_enc, node_id}  |
  |                               |                               |
  |<-- PAIRING_RESP (TempKey) -----|                               |
  | (Guarda LMK en NVS)           |                               |
  |                               |                               |
  |-- DATA_EVENT (LMK) ---------->| (Primer heartbeat/estado)     |
  |                               |-- MQTT PUBLISH -------------->|
  |                               |  topic: .../node/registered   |
  |                               |                               |
```

**Condiciones de Error y Timeouts:**
*   **Timeout Pairing:** Si `SRV-CORE-002` no responde en 10s, el Hub envía `PAIRING_RESP` con error al Nodo. El Nodo reintenta con backoff.
*   **Claim Inválido:** Si el servidor rechaza el `claim_code`, el Hub notifica al Nodo y aborta.
*   **Decisión de Diseño:** El Hub no genera la LMK localmente; la solicita al servidor para mantener la fuente de verdad de la criptografía en el backend (SRV-CORE-002).

---

### 3. Secuencia 2: Evento de Sensor -> Servidor -> Reglas -> Sirena

**Participantes:** `Nodo (PIR)`, `Hub-S3` (`t_espnow`, `t_logic`), `SRV-CORE-003`, `Nodo (Sirena)`.
**Objetivo:** Procesar intrusión, notificar y activar disuasión local.

```text
[PIR]      [Hub-S3 (t_espnow_rx)]  [Hub-S3 (t_logic)]  [SRV-CORE-003]  [Sirena]
  |               |                       |                   |               |
  |-- DATA_EVENT ->|                       |                   |               |
  | (Seq, CRC)    |-- Validar Seq/CRC -->|                   |               |
  |               |                       |                   |               |
  |<-- ACK --------| (< 50 ms)            |                   |               |
  |               |                       |                   |               |
  |               |-- Encolar xQ_Logic -->|                   |               |
  |               |                       |-- Evaluar SM_ALARM|               |
  |               |                       |  (Si ARMED -> TRIGGERED)          |
  |               |                       |                   |               |
  |               |                       |-- MQTT PUBLISH -->|               |
  |               |                       |  topic: .../event |               |
  |               |                       |  payload: {pir_1} |               |
  |               |                       |                   |               |
  |               |                       |                   |-- (Reglas Backend)
  |               |                       |                   |-- (Push Notif)
  |               |                       |                   |               |
  |               |                       |-- Encolar xQ_NodeTx               |
  |               |<-- xQ_NodeTx ---------|                   |               |
  |               |                       |                   |               |
  |               |-- CTRL_CMD (SIRENA_ON) ---------------------------------->|
  |               | (LMK, Seq)            |                   |               |
```

**Condiciones de Error y Timeouts:**
*   **Presupuesto RX->Publish:** El tiempo total desde la recepción del bit de stop del PIR hasta el `PUBLISH` en el socket TCP debe ser ≤ 250 ms.
*   **Fallo de Secuencia:** Si `seq <= last_seq`, el Hub descarta el frame silenciosamente (no envía ACK). El Nodo retransmitirá.
*   **Decisión de Diseño:** La sirena se activa por **lógica local** en el Hub (evaluación de `SM_ALARM`), no esperando el retorno del servidor. Esto garantiza la disuasión inmediata incluso si hay caída de internet. El servidor recibe el evento en paralelo para notificaciones.

---

### 4. Secuencia 3: Arm/Disarm vía PWA

**Participantes:** `PWA`, `SRV-CORE-003`, `Hub-S3` (`t_wifi_mqtt`, `t_logic`).
**Objetivo:** Cambio de estado del sistema desde la interfaz web.

```text
[PWA]                [SRV-CORE-003]               [Hub-S3 (t_wifi/t_logic)]
  |                       |                               |
  |-- POST /api/arm ------>|                               |
  |                       |-- MQTT PUBLISH (Retained) ---->|
  |                       |  topic: .../cmd/arm             |
  |                       |  payload: {pin_hash, delay}     |
  |                       |                               |
  |                       |                               |-- Validar PIN/Token
  |                       |                               |-- SM_ALARM: ARMED
  |                       |                               |   (Inicia Exit Delay)
  |                       |                               |
  |                       |<-- MQTT PUBLISH --------------|
  |                       |  topic: .../status              |
  |                       |  payload: {state: "arming"}     |
  |<-- 200 OK -----------|                               |
  | (UI: "Armando...")    |                               |
```

**Condiciones de Error y Timeouts:**
*   **Timeout de Comando:** Si el Hub no recibe el comando en 5s, la PWA muestra error de red.
*   **PIN Inválido:** Si el Hub detecta PIN incorrecto, publica `status: {error: "auth_fail"}` y no cambia de estado.
*   **Decisión de Diseño:** El `Exit Delay` (retardo de salida) se gestiona como un timer interno en `t_logic` durante la transición `DISARMED` -> `ARMED`. No se crea un estado FSM intermedio para optimizar RAM.

---

### 5. Secuencia 4: LWT Offline (Corte de Energía)

**Participantes:** `Hub-S3` (`t_sys_mon`, `t_wifi_mqtt`), `Broker MQTT`, `SRV-CORE-003`.
**Objetivo:** Notificar fallos de energía y caídas abruptas.

```text
[Hub-S3 (t_sys_mon)]    [Hub-S3 (t_wifi_mqtt)]    [Broker MQTT]      [SRV-CORE-003]
  |                             |                         |                   |
  |-- ADC detecta < 10V --------|                         |                   |
  | (Fallo red eléctrica)       |                         |                   |
  |                             |-- MQTT PUBLISH (QoS1) ->|                   |
  |                             |  topic: .../event         |                   |
  |                             |  payload: {power_loss}    |                   |
  |                             |                         |-- Forward -------->|
  |                             |                         |                   |-- Alerta "Corte Luz"
  |                             |                         |                   |
  | (Batería agotándose)        |                         |                   |
  |-- BROWNOUT DETECT ----------|                         |                   |
  |                             | (Corte abrupto / Reset) |                   |
  |                             |                         |                   |
  |                             X                         |-- (Timeout KeepAlive)
  |                             |                         |                   |
  |                             |                         |-- MQTT PUBLISH --->|
  |                             |                         |  topic: .../status  |
  |                             |                         |  payload: "offline" |
  |                             |                         |  (LWT Message)      |
  |                             |                         |                   |-- Alerta "Hub Caído"
```

**Condiciones de Error y Timeouts:**
*   **Flush de NVS:** Al detectar `power_loss`, `t_sys_mon` fuerza un commit de NVS (secuencias, estado alarma) antes de que el voltaje caiga al mínimo operativo. Si falla, se ignora para priorizar el envío MQTT.
*   **LWT vs Evento Explícito:** El evento `power_loss` es una notificación "graceful" (el Hub sabe que se va a apagar). El LWT `offline` es la confirmación "brutal" de que el Hub desapareció de la red (ej. corte de internet o muerte total).

---

### 6. Secuencia 5: OTA (Actualización Remota)

**Participantes:** `SRV-CORE-003`, `Hub-S3` (`t_wifi_mqtt`, Tarea OTA).
**Objetivo:** Actualizar firmware de forma segura con rollback.

```text
[SRV-CORE-003]                 [Hub-S3 (t_wifi/t_ota)]
  |                                   |
  |-- MQTT PUBLISH ------------------>|
  |  topic: .../cmd/ota                 |
  |  payload: {url, sha256, sig, ver}   |
  |                                   |
  |                                   |-- 1. Validar firma del manifiesto
  |                                   |-- 2. HTTPS GET binario -> partición ota_1
  |                                   |-- 3. Calcular SHA256 en flash
  |                                   |
  |                                   |-- (Si Hash/Sig != esperado) --> ABORT
  |                                   |
  |                                   |-- 4. esp_ota_set_boot_partition(ota_1)
  |                                   |-- 5. esp_restart()
  |                                   |
  | (Reboot en < 10s)                 |
  |                                   |-- Boot desde ota_1
  |                                   |-- Init FreeRTOS, WiFi, MQTT
  |                                   |
  |                                   |-- MQTT PUBLISH ------------------>|
  |<-- topic: .../status --------------|
  |   payload: {state: "online",       |
  |             fw_ver: "x.y.z"}       |
  |                                   |
  |                                   |-- (Si el boot falla o WDT resetea)
  |                                   |-- Bootloader revierte a ota_0
```

**Condiciones de Error y Timeouts:**
*   **Presupuesto Boot:** El tiempo desde `esp_restart()` hasta el `PUBLISH` de `status: online` debe ser ≤ 10 s.
*   **Fallo de Descarga:** Si la red cae durante la descarga, la tarea OTA aborta, borra la partición `ota_1` parcial y reintenta con backoff.
*   **Rollback:** Si el nuevo firmware hace un `panic` o no confirma el boot exitoso en 30s, el Watchdog de hardware resetea el sistema. El bootloader de ESP-IDF detecta que `ota_1` no fue confirmada y arranca automáticamente desde `ota_0`.

---

### 7. Registro de Decisiones de Diseño

1.  **Ejecución de Reglas de Sirena (Local vs Servidor):**
    *   *Decisión:* El Hub evalúa `SM_ALARM` localmente para activar la sirena.
    *   *Justificación:* La latencia de red (ida y vuelta al servidor) podría superar los 500ms. La disuasión física requiere inmediatez. El servidor solo procesa reglas para notificaciones push/SMS.
2.  **Manejo de LWT y Power Loss:**
    *   *Decisión:* Enviar evento MQTT `power_loss` antes de morir, y confiar en el LWT del Broker para la desconexión abrupta.
    *   *Justificación:* Permite al backend diferenciar entre un "Corte de luz" (el Hub avisó) y un "Fallo de hardware/internet" (el Hub desapareció). Esto cambia la severidad y el tipo de notificación al usuario final.
3.  **Validación de OTA en Dos Pasos:**
    *   *Decisión:* Validar firma del manifiesto (JSON) y luego hash del binario.
    *   *Justificación:* Evita descargar un binario de 1.5 MB si el manifiesto fue manipulado o expiró, ahorrando ancho de banda y tiempo de CPU.

---
**Fin de la Sección A5 v0.2**

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A5-01] Secuencias de Pairing, Evento, Arm/Disarm, LWT y OTA diagramadas con participantes y mensajes claros.
- [A5-02] Topics MQTT respetan estrictamente la estructura multi-tenant `tenant/{tid}/hub/{hid}/...` (B1, B3).
- [A5-03] Presupuestos de latencia (ACK <50ms, Boot <10s, RX->Publish <250ms) explícitos y justificados en las secuencias (B2).
- [A5-04] Condiciones de error, timeouts y mecanismos de rollback (OTA) detallados.
- [A5-05] Ausencia de código final confirmada; solo contratos de mensajería y diagramas ASCII (B4).
- [A5-06] Se eliminaron los términos "arduino" y "hardcode" de la documentación (Error estructural). }
--- CAMBIOS REALIZADOS ---
- [Error estructural] Se eliminaron los términos "arduino" y "hardcode" de la documentación.
- [A5-06] Se añadió punto de auditoría para confirmar la corrección de errores estructurales.}

--- AUDITORÍA ---
{{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A5-01] Secuencias de Pairing, Evento, Arm/Disarm, LWT y OTA diagramadas con participantes y mensajes claros.
- [A5-02] Topics MQTT respetan estrictamente la estructura multi-tenant `tenant/{tid}/hub/{hid}/...` (B1, B3).
- [A5-03] Presupuestos de latencia (ACK <50ms, Boot <10s, RX->Publish <250ms) explícitos y justificados en las secuencias (B2).
- [A5-04] Condiciones de error, timeouts y mecanismos de rollback (OTA) detallados.
- [A5-05] Ausencia de código final confirmada; solo contratos de mensajería y diagramas ASCII (B4). }ia }
