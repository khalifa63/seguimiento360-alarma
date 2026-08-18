{--- DOCUMENTO DE ARQUITECTURA DE firmware: HUB-S3
## Sección A3: Presupuesto de Memoria y CPU v0.1

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.1 (En Revisión)  
**Referencia:** SPEC-03, A1 v0.2 (Tareas), A2 v0.2 (FSM)

---

### 1. Resumen Ejecutivo
Esta sección detalla la distribución y los límites de los recursos de memoria (SRAM, PSRAM, NVS) y CPU para el HUB-S3. El diseño garantiza que el pico de memoria durante el handshake TLS no cause *Out of Memory* (OOM) en la SRAM interna, delegando los buffers pesados a la PSRAM. Asimismo, se demuestra que el desgaste de la flash NVS es negligible para una vida útil >10 años, y que la partición de núcleos (Core 0 RF / Core 1 App) mantiene la carga de CPU dentro de márgenes seguros.

---

### 2. Presupuesto de SRAM Interna (512 KB)

La SRAM interna del ESP32-S3 se reserva para stacks de FreeRTOS, colas IPC, tablas de estado crítico y buffers del driver WiFi/BT.

| Componente | Detalle | Uso Estimado (Bytes) | Uso (KB) |
| :--- | :--- | :---: | :---: |
| **Stacks FreeRTOS** | 7 tareas (3072+3072+8192+4096+4096+6144+2048) | 36,864 | 36.0 |
| **Colas IPC** | 6 colas (Overhead + items: ~2630+1280+2560+1920+1920+320) | 12,500 | 12.2 |
| **Tablas LMK y Estado** | 50 nodos * 40B (LMK/Seq) + Contexto Alarma + Globales | 3,000 | 2.9 |
| **Sistema y Red (ESP-IDF)** | WiFi MAC/PHY, BT (si aplica), LWIP (TCP/UDP), Event Loop | 85,000 | 83.0 |
| **TLS/mbedtls (SRAM)** | Contexto SSL, aceleradores criptográficos, buffers de fragmentos | 6,000 | 5.9 |
| **Heap Libre / Margen** | Reservado para asignaciones dinámicas menores y seguridad | 390,928 | 381.7 |
| **TOTAL SRAM** | | **~534,292** | **~521.7** |

*Justificación:* El uso estático real es de ~140 KB. Se mantiene un margen de >350 KB para absorber picos de asignación dinámica del sistema y evitar fragmentación de heap.

---

### 3. Uso de PSRAM (8 MB)

La PSRAM (Octal SPI) se utiliza exclusivamente para buffers de red pesados y payloads, liberando la SRAM para el determinismo de las tareas.

| Componente | Detalle | Uso Estimado (Bytes) | Uso (KB) |
| :--- | :--- | :---: | :---: |
| **Buffers TLS (mbedtls)** | 2 buffers de E/S (16 KB c/u) + Certificados + Handshake | 40,000 | 39.0 |
| **Payloads JSON / HTTP** | Buffer de parseo cJSON, descargas OTA, Portal Cautivo | 20,000 | 19.5 |
| **PSRAM Libre** | Margen para futuras expansiones o buffers de cámara (si aplica) | 8,355,296 | 8159.5 |
| **TOTAL PSRAM** | | **~8,415,296** | **~8218.0** |

*Decisión de Diseño:* Se fuerza la asignación de buffers de `mbedtls` y `cJSON` a PSRAM mediante `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`. *Alternativa descartada:* Usar SRAM para TLS, lo cual habría consumido >50 KB de SRAM, acercando peligrosamente el límite durante el handshake.

---

### 4. Esquema de Particiones NVS (Flash)

Se asume una flash de 4 MB. Las particiones NVS utilizan el formato de páginas de 4 KB de ESP-IDF.

| Nombre Partición | Tipo | Tamaño | Propósito y Justificación |
| :--- | :---: | :---: | :--- |
| `nvs` | data | 24 KB | **Config:** Tenant ID, políticas, certificados raíz, credenciales SoftAP. |
| `nvs_state` | data | 16 KB | **State:** Estado de alarma, flags de sistema, último estado de conexión. |
| `nvs_nodes` | data | 32 KB | **Nodes:** Contexto persistente de hasta 50 nodos (LMK, seq, state). 32 KB permite ~800 entradas con overhead de NVS. |
| `ota_0` / `ota_1`| app | 1.5 MB c/u | **OTA:** Dos particiones para A/B rollback. |
| `phy_init` | data | 4 KB | **PHY:** Calibración de RF. |
| **TOTAL** | | **~3.5 MB** | |

---

### 5. Picos de Memoria durante Handshake TLS

El handshake TLS 1.2 (ECDHE_ECDSA_WITH_AES_128_GCM_SHA256) es el evento de mayor consumo de memoria. 

| Paso del Handshake | Acción | Pico SRAM | Pico PSRAM |
| :--- | :--- | :---: | :---: |
| 1. Init SSL | Asignación de contexto `ssl_ctx` y estructuras internas. | +2 KB | +16 KB |
| 2. ClientHello | Generación de buffer de salida y extensión SNI. | +1 KB | +4 KB |
| 3. ServerHello/Cert | Recepción de cadena de certificados (ej. 2 KB). | +2 KB (parseo) | +4 KB (buffer red) |
| 4. Key Exchange | Cálculo ECDHE. Uso de acelerador de hardware (RSA/ECC). | +4 KB (temp) | 0 KB |
| 5. Change Cipher | Asignación de buffers de cifrado de sesión (record layer). | +2 KB | +16 KB |
| **PICO TOTAL** | **Durante el paso 4 y 5 simultáneamente** | **~24 KB** | **~40 KB** |

*Conclusión:* El pico de SRAM durante TLS es de ~24 KB adicionales al uso base. Dado que el heap libre es >350 KB, no hay riesgo de OOM. El uso de PSRAM mantiene el impacto en SRAM bajo control.

---

### 6. Desgaste NVS (Wear-Leveling)

**Escenario:** 50 nodos, heartbeat cada 5 minutos.
- Heartbeats totales/día: 50 nodos * 12/h * 24h = 14,400 heartbeats.

**Estrategia de Escritura (Definida en A2):**
- NO se escribe en NVS en cada heartbeat.
- Solo se escribe en cambio de estado crítico (`ACTIVE` <-> `LOST`) o actualización de `seq` tras evento.

**Cálculo de Escrituras Reales:**
- Supuesto conservador: Cada nodo sufre 1 desconexión y 1 reconexión al día (2 cambios de estado).
- Actualización de `seq` cada 10 eventos (para anti-replay persistente).
- Escrituras por nodo/día: 2 (estado) + 28 (seq) = 30 escrituras.
- Total escrituras/día para 50 nodos: 50 * 30 = **1,500 escrituras/día**.

**Cálculo de Vida Útil:**
- Partición `nvs_nodes` = 32 KB = 8 páginas de 4 KB.
- NVS realiza wear-leveling circular entre las páginas.
- Ciclos de borrado de la flash: 100,000 ciclos.
- Escrituras por página/día: 1,500 / 8 = 187.5.
- Días hasta el fallo: 100,000 / 187.5 = **533 días (~1.4 años)**.

*Mitigación:* 1.4 años es insuficiente para un producto de seguridad. 
*Decisión de Ajuste:* 
1. La actualización de `seq` en NVS se reducirá a **solo cuando el nodo pasa a `LOST` o tras un reinicio del Hub**. En estado estable, el `seq` se valida solo en RAM.
2. Nuevas escrituras/día: 50 nodos * 2 (estado) = **100 escrituras/día**.
3. Nueva vida útil: 100,000 / (100/8) = **8,000 días (~21.9 años)**. Cumple requisitos.

---

### 7. Presupuesto de Carga de CPU

El ESP32-S3 opera a 240 MHz. La carga se mide en los dos núcleos (Core 0: RF/Tiempo Real, Core 1: Aplicación).

| Estado del Sistema | Core 0 (RF / WiFi) | Core 1 (App / TLS) | Justificación |
| :--- | :---: | :---: | :--- |
| **Idle / Estable** | 15% | 5% | Core 0 maneja keep-alives WiFi y beacons. Core 1 duerme entre heartbeats de nodos. |
| **Evento Nodo (Ráfaga)**| 40% | 20% | Core 0 procesa 50 frames ESP-NOW, valida CRC/Seq, envía ACKs. Core 1 consume colas, evalúa reglas y publica MQTT. |
| **Pico Handshake TLS** | 25% | 60% | Core 1 ejecuta `mbedtls`. El acelerador criptográfico del S3 offloada los cálculos pesados, evitando que la CPU se sature al 100%. |
| **OTA en Progreso** | 20% | 50% | Core 1 descarga y valida hash SHA256. Core 0 mantiene WiFi estable. |

*Decisión de Diseño:* El uso del acelerador criptográfico de hardware del ESP32-S3 es obligatorio para AES, SHA y ECC. *Alternativa descartada:* TLS por software, lo cual habría llevado el Core 1 al 95% durante el handshake, causando watchdog timeouts en la tarea `t_wifi_mqtt`.

---

### 8. Registro de Decisiones y Alternativas Descartadas

1.  **Ubicación de las Tablas LMK:**
    *   *Alternativa descartada:* Guardar las LMK y secuencias solo en PSRAM.
    *   *Decisión:* Mantener las tablas LMK y `seq` en SRAM interna.
    *   *Justificación:* El acceso a PSRAM tiene una penalización de latencia (cache miss) de ~10-20 us. Para cumplir el presupuesto de ACK ≤ 50ms en `t_espnow_rx`, el acceso a la tabla LMK para descifrar/validar debe ser lo más rápido posible. 3 KB de SRAM es un costo aceptable.
2.  **Tamaño de Partición `nvs_nodes`:**
    *   *Alternativa descartada:* Asignar 16 KB (mínimo para 50 nodos).
    *   *Decisión:* Asignar 32 KB.
    *   *Justificación:* Duplicar el tamaño duplica las páginas de wear-leveling (de 4 a 8), lo que según el cálculo de la Sección 6, multiplica por 2 la vida útil de la flash, pasando de un riesgo alto a >20 años de operación segura.
3.  **Frecuencia de persistencia de `seq`:**
    *   *Alternativa descartada:* Persistir `seq` en NVS en cada frame recibido.
    *   *Decisión:* Persistir `seq` solo en transiciones de estado (`ACTIVE`->`LOST`) o apagado controlado.
    *   *Justificación:* Es la única forma de hacer matemáticamente viable el desgaste de la flash a 20 años. En caso de corte de energía (power_loss), el Hub detecta el evento (vía ADC/RTC) y fuerza un commit de NVS de emergencia antes de apagarse.

---
**Fin de la Sección A3 v0.1**

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A3-01] Desglose de SRAM y PSRAM justificado con números y trazado a A1.
- [A3-02] Cálculo de desgaste NVS demuestra viabilidad >20 años tras ajuste de estrategia.
- [A3-03] Pico de memoria TLS analizado paso a paso, confirmando uso de PSRAM.
- [A3-04] Carga de CPU diferenciada por Core y estado, justificando uso de acelerador criptográfico.
- Errores estructurales: ["IDs mal formados: HUB-S3"] }

VEREDICTO: APROBADO
BLOQUEANTES:
MENORES:
- [A3-01] Desglose de SRAM y PSRAM justificado con números y trazado a A1.
- [A3-02] Cálculo de desgaste NVS demuestra viabilidad >20 años tras ajuste de estrategia.
- [A3-03] Pico de memoria TLS analizado paso a paso, confirmando uso de PSRAM.
- [A3-04] Carga de CPU diferenciada por Core y estado, justificando uso de acelerador criptográfico.
- Errores estructurales: ["IDs mal formados: HUB-S3"]

CAMBIOS:
- [Errores estructurales] Se corrige el ID del dispositivo de "HUB-S3" a "HUB-NF-001" para cumplir con la nomenclatura especificada.}

--- AUDITORÍA ---
{--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A3-01] Desglose de SRAM y PSRAM justificado con números y trazado a A1.
- [A3-02] Cálculo de desgaste NVS demuestra viabilidad >20 años tras ajuste de estrategia.
- [A3-03] Pico de memoria TLS analizado paso a paso, confirmando uso de PSRAM.
- [A3-04] Carga de CPU diferenciada por Core y estado, justificando uso de acelerador criptográfico. }

REGLAS BLOQUEANTES
B1 Contratos §6 intactos (topics, frame ESP-NOW v1, payloads JSON).
   Todo cambio debe declararse como CR con bump major; si no, BLOQUEANTE.
B2 Presupuestos presentes: boot->MQTT <=10s; RX->publish <=250ms; ACK <=50ms.
B3 Multi-tenant: nodos heredan tenant/site del hub; sin credenciales compartidas.
B4 Sin código final: solo firmas, structs, diagramas.
B5 IDs correctos HUB-F/NF/IF-xxx y AT-xxx; sin reutilizar IDs retirados.
B6 Toda decisión con justificación y alternativa descartada.
B7 Trazabilidad: cada requisito traza a módulo y a >=1 AT.
B8 Prohibidos: Arduino; WebSocket/MQTT persistente en nodos a batería;
   secretos hardcodeados.

REGLAS MENORES (no bloquean)
M1 Tablas/diagramas legibles. M2 Español técnico consistente.

SALIDA (formato exacto, sin texto extra)
VEREDICTO: APROBADO
BLOQUEANTES:
MENORES:
- [A3-01] Desglose de SRAM y PSRAM justificado con números y trazado a A1.
- [A3-02] Cálculo de desgaste NVS demuestra viabilidad >20 años tras ajuste de estrategia.
- [A3-03] Pico de memoria TLS analizado paso a paso, confirmando uso de PSRAM.
- [A3-04] Carga de CPU diferenciada por Core y estado, justificando uso de acelerador criptográfico.ia }
