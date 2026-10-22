# DOCUMENTO DE ARQUITECTURA DE firmware: HUB-S3
## Sección A2: Máquinas de Estado v0.2

**Autor:** Arquitecto Jefe de Firmware
**Proyecto:** Seguimiento 360
**Estado:** v0.2 (Aprobado para Implementación)
**Referencia:** SPEC-03 (Acceptance Tests), SRV-CORE-002, SRV-CORE-003

---

### 1. Resumen Ejecutivo
Esta sección define las cuatro Máquinas de Estado Finito (FSM) críticas para el HUB-S3. El diseño prioriza el determinismo, la recuperación ante fallos (watchdog/NVS) y el cumplimiento estricto de los presupuestos de tiempo. Cada estado y transición está trazado a las Pruebas de Aceptación (AT) de la SPEC-03 para garantizar la cobertura de requisitos.

---

### 2. SM_PROV: Máquina de Estados de Provisioning
Gestiona el ciclo de vida inicial del Hub, desde el factory reset hasta la vinculación con un Tenant (SRV-CORE-002).

~~text
[FACTORY_RESET] --(NVS vacío / Botón 10s)--> [PROV_LISTEN]
^                                            |
|                                            | (Éxito SRV-CORE-002)
|                                            v
+-------------(Factory Reset)---------- [ACTIVE_TENANT]
~~

| Estado | Descripción | Condiciones de Salida (Transición) | AT SPEC-03 |
| :--- | :--- | :--- | :--- |
| `FACTORY_RESET` | Estado inicial o post-reset físico. Limpia NVS de tenant y nodos. | NVS formateado -> Transición a `PROV_LISTEN`. | AT-05 (Integridad NVS) |
| `PROV_LISTEN` | Hub levanta SoftAP/HTTP local o BLE. Espera *Claim Code*. | Recibe *Claim Code* válido -> Valida con Servidor -> Transición a `ACTIVE_TENANT`. | AT-04 (Provisioning) |
| `ACTIVE_TENANT` | Hub operativo. Tenant ID y políticas guardadas en NVS. | Botón físico 10s -> Transición a `FACTORY_RESET`. | AT-04, AT-05 |

**Decisiones de Diseño:**
- *Alternativa descartada:* Provisioning vía serial/UART. *Descartada* por falta de ergonomía para el instalador y riesgo de seguridad. Se usa SoftAP + Portal Cautivo local.
- *Persistencia:* El `tenant_id` se escribe en NVS solo tras confirmar la respuesta de SRV-CORE-002.

---

### 3. SM_CONN: Máquina de Estados de Conexión (WiFi/MQTT)
Gestiona la pila de red externa. Diseñada para cumplir el presupuesto de Boot ≤ 10s y manejar tormentas de reconexión.

~~text
[DISCONNECTED] --(Init)--> [WIFI_CONNECTING] --(Éxito)--> [WIFI_CONNECTED]
^                          |                           |
|                          | (Fallo/Timeout)           | (Init MQTT)
|                          v                           v
+-------------------- [ERROR_BACKOFF] <--------- [MQTT_CONNECTING]
                                                      |
                                                      | (CONNACK OK)
                                                      v
                                                   [ONLINE]
~~

| Estado | Descripción | Condiciones de Salida (Transición) | AT SPEC-03 |
| :--- | :--- | :--- | :--- |
| `DISCONNECTED` | Sin red. Espera inicio de tareas. | Tarea de conexión iniciada -> `WIFI_CONNECTING`. | AT-01 (Boot) |
| `WIFI_CONNECTING` | Escaneo y asociación WiFi. | IP obtenida -> `WIFI_CONNECTED`. Fallo -> `ERROR_BACKOFF`. | AT-01 |
| `WIFI_CONNECTED` | WiFi up, sin MQTT. | LWT configurada, inicio handshake TLS -> `MQTT_CONNECTING`. | AT-01 |
| `MQTT_CONNECTING` | Handshake WSS+TLS y envío CONNECT. | CONNACK recibido -> `ONLINE`. Fallo -> `ERROR_BACKOFF`. | AT-01, AT-07 |
| `ONLINE` | Sesión MQTT establecida. QoS1 activo. | Disconnect/Keepalive fail -> `WIFI_CONNECTED` o `ERROR_BACKOFF`. | AT-01, AT-03 |
| `ERROR_BACKOFF` | Espera exponencial (1s -> 60s + jitter). | Timer expira -> `WIFI_CONNECTING` (o `MQTT_CONNECTING` si WiFi está up). | AT-01 |

**Decisiones de Diseño:**
- *Desacople WiFi/MQTT:* Si MQTT falla (ej. servidor caído), el Hub mantiene WiFi conectado para no perder sincronización NTP ni capacidad de OTA local, evitando el escaneo WiFi repetitivo (ahorra CPU/RAM).
- *LWT (Last Will and Testament):* Se registra en `WIFI_CONNECTED`. Si el Hub muere, el broker publica "offline". En `ONLINE`, se publica status "online" con QoS1 y *retain*.

---

### 4. SM_ALARM: Máquina de Estados de Alarma
Lógica de seguridad local. Procesa eventos de nodos y comandos MQTT/Keypad.

~~text
[DISARMED] --(Cmd Arm)--> [ARMED] --(Sensor Trip)--> [TRIGGERED] --(Timeout)--> [ALARM]
^                       ^                          |                          |
|                       |                          | (Cmd Disarm)             | (Cmd Disarm)
+-----------------------+--------------------------+--------------------------+
~~

| Estado | Descripción | Condiciones de Salida (Transición) | AT SPEC-03 |
| :--- | :--- | :--- | :--- |
| `DISARMED` | Sistema inactivo. Nodos en modo baja potencia (si aplica). | Cmd ARM válido -> `ARMED`. | AT-09 (Lógica Alarma) |
| `ARMED` | Sistema activo. Monitoreo de sensores perimetrales/interiores. | Sensor trip (PIR/Puerta) -> `TRIGGERED`. Cmd DISARM -> `DISARMED`. | AT-09 |
| `TRIGGERED` | Retardo de entrada (Entry Delay). Sirena NO activa aún. | Timeout retardo -> `ALARM`. Cmd DISARM -> `DISARMED`. | AT-09 |
| `ALARM` | Sirena activa. Notificaciones máximas al servidor. | Cmd DISARM -> `DISARMED`. Timeout silencio -> `DISARMED`. | AT-03, AT-09 |

**Decisiones de Diseño:**
- *Retardos (Delays):* `ARMED` incluye implícitamente el "Exit Delay" (no mostrado como estado para simplificar, pero gestionado como timer en transición). `TRIGGERED` es el "Entry Delay".
- *Evaluación de Reglas:* La transición de `ARMED` a `TRIGGERED` no es directa por hardware; pasa por el motor de reglas local (ej. si el sensor es de "interior" y hay "perímetro" violado primero, ignora). Esto se gestiona en la tarea `logic`, no en la FSM directa.

---

### 5. SM_NODE: Máquina de Estados de Ciclo de Nodo
Gestiona la vida de los dispositivos ESP-NOW (PIR, puertas, etc.) desde la perspectiva del Hub.

~~text
[UNKNOWN] --(Pairing Req)--> [PAIRING] --(Validado)--> [ACTIVE] --(Timeout Heartbeat)--> [LOST]
^                                                    ^                                 |
|                                                    |                                 |
|                                                    +-------(Heartbeat RX)------------+
|                                                                                      |
+----------------------------------------(Timeout Extremo / Delete Cmd)----------------+
~~

| Estado | Descripción | Condiciones de Salida (Transición) | AT SPEC-03 |
| :--- | :--- | :--- | :--- |
| `UNKNOWN` | MAC recibida en ESP-NOW pero no en NVS. | Solicitud de Pairing -> `PAIRING`. | AT-02, AT-10 |
| `PAIRING` | Hub esperando validación de SRV-CORE-002 o política local. | Aprobado + LMK generado -> `ACTIVE`. Rechazado -> `UNKNOWN`. | AT-04, AT-07 |
| `ACTIVE` | Nodo operativo. LMK en NVS. Heartbeats normales. | Faltan 3 heartbeats -> `LOST`. Cmd Delete -> `UNKNOWN`. | AT-02, AT-10 |
| `LOST` | Nodo sin comunicación. Posible batería agotada. | Recibe Heartbeat -> `ACTIVE`. Timeout 24h -> `UNKNOWN` (Limpia NVS). | AT-10 |

**Decisiones de Diseño:**
- *Wear-leveling NVS:* El contador de heartbeats fallidos no se escribe en NVS en cada fallo. Se mantiene en RAM. Solo el cambio de estado (`ACTIVE` -> `LOST`) o la recepción de un heartbeat válido (`LOST` -> `ACTIVE`) dispara escritura NVS para persistir el estado del nodo.
- *Anti-replay:* Cada nodo tiene su propia ventana de secuencia (`seq`) en NVS.

---

### 6. Definiciones de Datos (Firmas y Structs de Diseño)
A continuación, las estructuras de datos que alimentan las FSM. No son código compilable, son contratos de diseño.

~~c
// --- Enumeraciones de Estados ---
typedef enum {
    SM_PROV_FACTORY_RESET = 0,
    SM_PROV_LISTEN,
    SM_PROV_ACTIVE_TENANT
} sm_prov_state_t;

typedef enum {
    SM_CONN_DISCONNECTED = 0,
    SM_CONN_WIFI_CONNECTING,
    SM_CONN_WIFI_CONNECTED,
    SM_CONN_MQTT_CONNECTING,
    SM_CONN_ONLINE,
    SM_CONN_ERROR_BACKOFF
} sm_conn_state_t;

typedef enum {
    SM_ALARM_DISARMED = 0,
    SM_ALARM_ARMED,
    SM_ALARM_TRIGGERED,
    SM_ALARM_ALARM
} sm_alarm_state_t;

typedef enum {
    SM_NODE_UNKNOWN = 0,
    SM_NODE_PAIRING,
    SM_NODE_ACTIVE,
    SM_NODE_LOST
} sm_node_state_t;

// --- Contexto de la FSM de Conexión (Ejemplo) ---
typedef struct {
    sm_conn_state_t current_state;
    uint32_t backoff_ms;           // Tiempo actual de backoff (1s a 60s)
    uint8_t  retry_count;          // Contador para cálculo de backoff
    uint32_t last_state_change_ms; // Timestamp para watchdog de estados
    bool     tls_handshake_done;   // Flag para optimización de memoria
} fsm_conn_ctx_t;

// --- Contexto de Nodo (Persistido parcialmente en NVS) ---
typedef struct {
    uint8_t  mac[6];               // Identificador físico
    uint8_t  lmk[16];              // Long Term Key (ESP-NOW)
    sm_node_state_t state;         // Estado actual
    uint32_t seq_rx;               // Última secuencia RX (Anti-replay)
    uint32_t last_heartbeat_ms;    // Timestamp último heartbeat (RAM)
    uint8_t  missed_heartbeats;    // Contador de fallos (RAM)
} node_ctx_t;
~~

---

### 7. Registro de Decisiones y Alternativas Descartadas
1.  **FSM Única vs. FSM Ortogonales:**
    - *Decisión:* Se utilizan 4 FSM ortogonales (independientes) que corren en tareas separadas o comparten tarea con prioridades distintas.
    - *Justificación:* Una FSM monolítica (ej. no puede procesar nodos si no hay MQTT) violaría el presupuesto de ACK ≤ 50ms. La ortogonalidad permite que `SM_NODE` genere ACKs en `SM_CONN_ERROR_BACKOFF`.

2.  **Manejo de "Exit Delay" en Alarma:**
    - *Decisión:* No crear un estado `ARMING` explícito.
    - *Justificación:* El retardo de salida se maneja como un timer en la transición de `DISARMED` a `ARMED`. Durante este timer, los sensores de salida son ignorados. Simplifica la FSM y reduce el consumo de RAM en tablas de transición.

3.  **Persistencia de Node State:**
    - *Decisión:* `last_heartbeat_ms` y `missed_heartbeats` viven en RAM.
    - *Justificación:* Escribir en NVS en cada heartbeat (ej. cada 5 min por 50 nodos) destruiría la flash en meses. Solo los cambios de estado críticos (`ACTIVE` <-> `LOST`) tocan NVS.

---

**Fin de la Sección A2 v0.2**
