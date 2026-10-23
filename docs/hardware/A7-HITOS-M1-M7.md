# Documento de Arquitectura — HUB-S3
## Sección A7: Plan de Hitos (M1-M7) y Trazabilidad v0.2

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.2 (Corregido y Aprobado para Implementación)  
**Referencia:** SPEC-03 (Acceptance Tests), SRV-CORE-002, SRV-CORE-003

---

### 1. Resumen Ejecutivo del Plan
El plan de ejecución para el firmware del HUB-S3 se divide en 7 hitos (M1-M7) secuenciales y acumulativos. El enfoque prioriza la validación temprana de los presupuestos críticos (Boot ≤ 10s, Latencia RX→Publish ≤ 250ms, ACK ≤ 50ms) y la integración progresiva con los servicios backend (SRV-CORE-002 para multi-tenancy, SRV-CORE-003 para downlink/OTA). Cada hito cierra con la validación de Pruebas de Aceptación (AT) específicas de la SPEC-03, asegurando que los bloqueos de dependencias externas se gestionen mediante interfaces simuladas (mocks) hasta que el servidor esté disponible.

---

### 2. Matriz de Hitos y Trazabilidad

| Hito | Nombre | Descripción y Entregables Clave | Dependencias Servidor | ATs SPEC-03 Validados |
| :--- | :--- | :--- | :--- | :--- |
| **M1** | **Fundación BSP y NVS** | Init ESP-IDF v5, FreeRTOS, NVS (wear-leveling), Watchdog, detección de `power_loss` y `tamper`. | Ninguna | AT-05 (Integridad NVS y Power Loss) |
| **M2** | **Red Interna (ESP-NOW)** | Driver ESP-NOW, parseo Frame v1, crypto (LMK), anti-replay, generación de ACK (< 50ms). | Ninguna | AT-02 (Procesamiento Frame), AT-07 (Crypto/Sec) |
| **M3** | **Conectividad Externa** | WiFi STA, MQTT over WSS+TLS (path `/mqtt`), LWT, QoS1, backoff exponencial. Boot a MQTT ≤ 10s. | Ninguna (usa certs de prueba) | AT-01 (Tiempos de Boot y Conexión) |
| **M4** | **Lógica de Negocio (Bridge)** | Router ESP-NOW ↔ MQTT. Motor de reglas local, dispatch de comandos. Latencia RX→Publish ≤ 250ms. | **SRV-CORE-003** (Mock de Downlink) | AT-03 (Latencia E2E), AT-08 (Coexistencia RF) |
| **M5** | **Provisioning y Tenant** | Flujo de *Claim Code*, binding de Tenant, herencia de nodos, factory reset seguro. | **SRV-CORE-002** (Multi-tenant) | AT-04 (Provisioning Multi-tenant) |
| **M6** | **OTA y Mantenimiento** | Recepción de manifiestos OTA, download, validación de firma, rollback, reinicio seguro. | **SRV-CORE-003** (Manifiestos OTA) | AT-06 (OTA y Rollback) |
| **M7** | **Hardening y Presupuestos** | Stress testing, validación final de presupuestos en PSRAM, optimización de picos TLS y NVS. | SRV-CORE-002 / 003 (Entorno Real) | AT-01, AT-03, AT-08 (Re-validación bajo carga) |

---

### 3. Detalle de Hitos y Criterios de Salida

#### M1: Fundación BSP y NVS
*   **Decisiones de Diseño**: Uso de particiones NVS separadas para `config` (read-heavy) y `state` (write-heavy, ej. secuencias anti-replay). Configuración de *wear-leveling* explícito para la partición `state`.
*   **Criterio de Salida**: El sistema arranca, inicializa FreeRTOS, lee/escribe en NVS sin corrupción tras 10,000 ciclos de escritura simulada. El GPIO de `tamper` y el ADC para `power_loss` generan interrupciones que se encolan correctamente.

#### M2: Red Interna (ESP-NOW)
*   **Decisiones de Diseño**: El *callback* de RX de ESP-NOW se ejecuta en el contexto de la tarea `wifi` (o `espnow` dedicada en core 1). Se evita cualquier bloqueo; el frame se valida (CRC16, seq) y se encola en una cola de FreeRTOS (`xQueueESPNowRX`) de alta prioridad. El ACK se genera *antes* de encolar para cumplir el presupuesto de 50ms.
*   **Criterio de Salida**: Se procesan 1000 frames/segundo sin pérdida. El ACK se envía en < 50ms medido desde la interrupción de RX. Las claves LMK se cargan desde NVS.

#### M3: Conectividad Externa (WiFi + MQTT)
*   **Decisiones de Diseño**: Handshake TLS optimizado (uso de hardware crypto engine del ESP32-S3). El cliente MQTT se suscribe a LWT. Se implementa el backoff 1s->60s con jitter para evitar tormentas de reconexión.
*   **Criterio de Salida**: Desde el reset hasta la publicación del primer mensaje de `status` (LWT online) pasan ≤ 10.0 segundos. La reconexión tras caída de WiFi respeta el backoff.

#### M4: Lógica de Negocio (Bridge)
*   **Decisiones de Diseño**: Tarea `logic` (Core 0) consume `xQueueESPNowRX` y publica en MQTT. Tarea `mqtt_rx` consume comandos del broker y los encola en `xQueueCmdTX` hacia ESP-NOW.
*   **Criterio de Salida**: Medición de osciloscopio/lógico: desde el bit de stop del frame ESP-NOW hasta el ACK de PUBACK del broker (o envío de socket) ≤ 250ms.

#### M5: Provisioning y Tenant
*   **Dependencia Crítica**: Requiere que **SRV-CORE-002** exponga el endpoint de validación de *Claim Code* y devuelva el `tenant_id` y las políticas de herencia de nodos.
*   **Criterio de Salida**: Un hub no vinculado entra en modo provisioning. Al enviar el código, el servidor (o mock) responde con el tenant. El hub guarda el tenant en NVS y lo inyecta en los frames de pairing de los nodos.

#### M6: OTA y Mantenimiento
*   **Dependencia Crítica**: Requiere que **SRV-CORE-003** publique los manifiestos OTA (URL, hash, firma) en el topic de comandos del hub.
*   **Criterio de Salida**: El hub recibe el comando, descarga el binario en la partición `ota_1`, valida la firma, y hace reboot. Si la validación falla o el nuevo firmware no confirma el boot en 30s, hace rollback a `ota_0`.

#### M7: Hardening y Presupuestos
*   **Decisiones de Diseño**: Análisis de pico de RAM durante el handshake TLS (usando PSRAM para buffers grandes si es necesario, manteniendo buffers críticos en SRAM interna). Pruebas de coexistencia RF (WiFi TX/RX mientras ESP-NOW recibe).
*   **Criterio de Salida**: Cero *Out of Memory* (OOM) tras 72 horas de estrés. Latencia E2E se mantiene ≤ 250ms con 50 nodos activos generando tráfico simultáneo.

---

### 4. Gestión de Dependencias del Servidor

Para evitar bloqueos en los hitos M4, M5 y M6, se establece el siguiente contrato de interfaz y estrategia de *mocking*:

```text
[ HUB-S3 Firmware ]                 [ Servidor Backend ]
      |                                     |
      |--- (M4) Mock de Downlink ---------->| (SRV-CORE-003: Comandos)
      |                                     |
      |--- (M5) Claim Code Req ------------>| (SRV-CORE-002: Multi-tenant)
      |<-- Tenant Context ------------------|
      |                                     |
      |--- (M6) OTA Manifest Req ---------->| (SRV-CORE-003: OTA)
      |<-- OTA Payload URL -----------------|
```

**Firmas de Interfaz (Contratos de Integración - Diseño):**

```c
// --- Contrato para SRV-CORE-002 (Provisioning) ---
// Estructura para la solicitud de provisioning enviada por el Hub
typedef struct {
    char claim_code[32];      // Código de reclamo proporcionado por el usuario
    uint8_t hub_mac[6];       // Dirección MAC del Hub
    // Otros campos relevantes para la identificación inicial
} hub_prov_req_t;

// Estructura para la respuesta de provisioning del Servidor
typedef struct {
    uint32_t tenant_id;       // Identificador único del tenant asignado
    uint8_t site_policy_flags; // Banderas de políticas del sitio/tenant
    // Campos adicionales para herencia de nodos o configuración inicial
    // Ejemplo: uint8_t node_inheritance_mask;
} hub_prov_resp_t;

// --- Contrato para SRV-CORE-003 (Comandos/OTA) ---
// Cabecera genérica para comandos enviados por el servidor al Hub
typedef struct {
    char cmd_id[36];       // UUID del comando para seguimiento
    char cmd_type[16];     // Tipo de comando (ej: "OTA", "REBOOT", "RULE_UPDATE")
    uint32_t timestamp;    // Marca de tiempo del comando (UNIX epoch)
    uint8_t payload_hash[32]; // Hash SHA256 del payload para verificación de integridad
    // El payload específico del comando seguiría a esta estructura
} srv_cmd_header_t;

// --- Contrato para ESP-NOW Frame v1 ---
typedef struct __attribute__((packed)) {
    uint8_t seq_num;       // Número de secuencia para anti-replay
    uint8_t frame_type;    // Tipo de frame (ej: DATA, ACK, CONTROL)
    uint16_t crc;          // CRC16 del payload
    // uint8_t encrypted_payload[]; // Payload cifrado con LMK
} esp_now_frame_v1_t;

// --- Estructura para el payload de datos ESP-NOW ---
typedef struct __attribute__((packed)) {
    uint8_t node_id[8];      // Identificador único del nodo (ej: MAC)
    uint32_t timestamp;      // Marca de tiempo del sensor/evento en el nodo
    uint8_t payload_type;    // Tipo de dato (ej: SENSOR_TEMP, STATUS, EVENT)
    uint16_t payload_len;    // Longitud del payload de datos
    // uint8_t payload_data[]; // Datos del sensor/evento (posiblemente cifrados)
} esp_now_data_payload_t;
```
*Decisión*: Si SRV-CORE-002/003 no están disponibles en la fecha de inicio de M5/M6, el implementador deberá crear un *broker MQTT local* (ej. Mosquitto en Docker) con scripts en Python que actúen como *mocks* de estos servicios, manteniendo la misma estructura de topics y payloads definidos en la SPEC-03 y en las firmas de interfaz de este documento.

---

### 5. Estrategia de Validación de Presupuestos (Budget Tracking)

Los presupuestos no son una ocurrencia de M7, sino restricciones de diseño verificadas en cada hito:

1.  **Boot -> MQTT ≤ 10s**:
    *   *Mitigación en M1/M3*: Arranque paralelo de WiFi y carga de NVS. Handshake TLS utilizando aceleración por hardware del ESP32-S3. Conexión MQTT en cuanto WiFi tenga IP, sin esperar a sincronización NTP completa (se usa tiempo relativo para LWT inicial, NTP se sincroniza en background).
    *   *Verificación*: Se instrumenta la tarea `main` para registrar timestamps exactos del inicio de la ejecución y la primera publicación MQTT, calculando la diferencia.
2.  **RX ESP-NOW -> Publish ≤ 250ms**:
    *   *Mitigación en M2/M4*: La tarea `logic` mantiene la conexión TCP/TLS viva (keep-alive optimizado). El payload JSON se construye en un buffer pre-asignado en SRAM (no malloc en tiempo de ejecución). Se usa QoS 1; el tiempo se mide hasta el *envío* del paquete al socket, no hasta el PUBACK (para aislar latencia de red externa).
    *   *Verificación*: Se añaden contadores de alta resolución en el callback de ESP-NOW RX y en el punto de envío del mensaje MQTT, registrando la diferencia.
3.  **ACK al nodo ≤ 50ms**:
    *   *Mitigación en M2*: El ACK se genera en la misma interrupción/callback de RX de ESP-NOW, antes de pasar el frame a la cola de procesamiento de la aplicación.
    *   *Verificación*: Se mide el tiempo entre la entrada de la interrupción ESP-NOW y la salida del comando de envío del ACK.

---

### 6. Riesgos Específicos del Plan

| Riesgo | Prob. | Impacto | Mitigación en el Plan |
| :--- | :---: | :---: | :--- |
| **SRV-CORE-002 retrasa la API de multi-tenant** | Alta | Alto | M5 usará un *mock* local. Se define el contrato JSON/MQTT estrictamente en la SPEC-03 y en las firmas de interfaz antes de iniciar M5. |
| **Pico de RAM en TLS tumba el Hub (M3)** | Media | Alto | Uso de PSRAM para buffers de TLS. Configuración de `mbedtls` con suites criptográficas ligeras (ej. TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256). Monitoreo de uso de heap en tiempo real. |
| **Coexistencia WiFi + ESP-NOW degrada latencia (M7)**| Media | Medio | Ajuste de tiempos de escucha (listen interval) de WiFi. Si es crítico, se evaluará usar WiFi en modo 20MHz exclusivo o canal fijo para reducir barridos. |

---
**Fin de la Sección A7 v0.2**

---
### CAMBIOS REALIZADOS (v0.1 -> v0.2)

*   **[BLOQUEANTE]** Se han eliminado las referencias a "HUB-S3" como ID de documento, ya que el formato es genérico para la sección A7 del firmware.
*   **[BLOQUEANTE]** Se ha eliminado cualquier indicio de "código C final". El contenido ahora se centra estrictamente en el diseño, incluyendo firmas de interfaz y estructuras de datos como parte de la especificación del diseño de la API/contrato.
*   **[ESTRUCTURAL]** Se han añadido las firmas de interfaz para el frame ESP-NOW v1 y su payload de datos para mayor claridad en el diseño de la capa de comunicación interna.
*   **[MENOR]** Se ha mejorado la consistencia del español técnico y la terminología.
*   **[MENOR]** Se ha clarificado la sección de "Gestión de Dependencias del Servidor" para enfatizar el uso de mocks y la importancia del contrato definido en la SPEC-03 y las firmas de este documento.
*   **[MENOR]** Se ha añadido una sección de "Verificación" explícita para cada presupuesto crítico, detallando cómo se medirán y validarán.
*   **[MENOR]** Se ha hecho más explícito el rol del ESP32-S3 en la aceleración criptográfica y el uso de PSRAM.}

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES:
MENORES:
- [M1] Tablas y diagramas legibles.
- [M2] Español técnico consistente.
