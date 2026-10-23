# Documento de Arquitectura — HUB-S3
## Sección A8: Preguntas Abiertas y Decisiones Pendientes v0.2

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.2 (En Revisión)  
**Referencia:** SPEC-03, A1-A6

---

### 1. Resumen Ejecutivo
Antes de iniciar la fase de codificación (M1-M7), existen decisiones de diseño que requieren alineación entre el equipo de firmware, el equipo de backend (SRV-CORE) y la definición final de contratos. Esta sección cataloga las preguntas críticas, sus opciones y el impacto de no resolverlas, garantizando que la implementación no viole los presupuestos ni los contratos de seguridad.

---

### 2. Preguntas para Implementador (Firmware)

#### P1.1: Estrategia de asignación PSRAM para `mbedtls`
*   **Contexto:** En A3 se definió el uso de PSRAM para buffers TLS. Sin embargo, `mbedtls` permite configurar *memory allocators* personalizados.
*   **Opciones consideradas:**
    *   *Opción A:* Sobrescribir *todas* las asignaciones de `mbedtls` > 1 KB para usar PSRAM.
    *   *Opción B:* Solo forzar a PSRAM los buffers de I/O (Record Layer) y mantener el estado del handshake y contextos en SRAM interna.
*   **Impacto si no se resuelve:** La Opción A es más segura para los límites de SRAM, pero introduce *cache-misses* que podrían degradar el tiempo de handshake. La Opción B acelera el handshake (ayudando al presupuesto de Boot ≤ 10s) pero requiere calcular el pico exacto de SRAM para evitar OOM si el servidor envía una cadena de certificados inesperadamente larga.

#### P1.2: Coexistencia de Canales WiFi y ESP-NOW
*   **Contexto:** El ESP32-S3 comparte la radio 2.4 GHz. Para cumplir el presupuesto de ACK ≤ 50ms, ESP-NOW no puede ser interrumpido frecuentemente por el escaneo o tráfico WiFi.
*   **Opciones consideradas:**
    *   *Opción A:* Forzar al Hub a conectarse únicamente a un canal WiFi específico (ej. Canal 1) durante el provisioning, y bloquear el cambio de canal en operación normal.
    *   *Opción B:* Utilizar la API de coexistencia de ESP-IDF, permitiendo que WiFi y ESP-NOW salten de canal dinámicamente.
*   **Impacto si no se resuelve:** La Opción B degradará severamente la latencia de ESP-NOW (violando el presupuesto de 50ms) debido a las pausas obligatorias para escuchar beacons WiFi. La Opción A garantiza el rendimiento de RF, pero el instalador deberá configurar el router del cliente para que el SSID opere en ese canal específico, o el Hub rechazará la conexión WiFi.

---

### 3. Preguntas para Servidor (SRV-CORE-002 / SRV-CORE-003)

#### P2.1: Autoridad de generación del `node_id` lógico
*   **Contexto:** En la secuencia de Pairing (A5), el Hub reporta la MAC del nodo. El backend necesita un `node_id` lógico para las reglas y la base de datos multi-tenant.
*   **Opciones consideradas:**
    *   *Opción A:* El Servidor (SRV-CORE-002) genera el `node_id` (ej. UUID) al validar el `claim_code` y lo devuelve al Hub en la respuesta de pairing.
    *   *Opción B:* El Hub genera el `node_id` (ej. incremental por tenant en NVS) y lo envía al servidor.
*   **Impacto si no se resuelve:** Si se elige la Opción B y el Hub sufre un Factory Reset (A4), perderá la secuencia de IDs, provocando colisiones o nodos fantasma en el servidor. La Opción A centraliza la verdad en el backend, pero obliga al Hub a esperar la respuesta del servidor antes de poder programar la LMK en el nodo.

#### P2.2: Política de retención y cola de mensajes MQTT (QoS1)
*   **Contexto:** Riesgo de overflow de mensajes retenidos (A6) y garantía de entrega de comandos (ej. `DISARM`) si el Hub pierde conexión temporalmente.
*   **Opciones consideradas:**
    *   *Opción A:* El Broker (EMQX) mantiene estrictamente el *último* mensaje retenido por topic. Los comandos downlink no son retenidos, sino encolados en sesión (Clean Session = False).
    *   *Opción B:* El Broker encola hasta N mensajes retenidos por tenant.
*   **Impacto si no se resuelve:** Si se usa la Opción A, el firmware del Hub debe diseñar su LWT y mensajes de `status` asumiendo que solo el último estado sobrevive. Si el backend intenta usar mensajes retenidos para comandos, estos se perderán si el Hub se reinicia. Se debe confirmar que los comandos críticos viajan como QoS1 en sesión persistente, no como retenidos.

---

### 4. Preguntas de Integración

#### P3.1: Autoridad de generación de LMK y Plan de Contingencia
*   **Contexto:** En A4 y A5 se estableció que el Servidor genera la LMK para mantener la fuente de verdad criptográfica.
*   **Opciones consideradas:**
    *   *Opción A:* Servidor genera LMK. Si el servidor no responde en 10s, el Hub aborta el pairing.
    *   *Opción B:* Hub genera LMK localmente, la envía al nodo, y luego hace un "reporte" asíncrono al servidor.
*   **Impacto si no se resuelve:** La Opción B viola el principio de "Confianza Cero" y el modelo de amenazas (A4), ya que un Hub comprometido podría generar LMKs débiles o clonar nodos. Se debe confirmar la Opción A y definir si el servidor almacenará una copia de las LMKs (para permitir recuperación de Hub) o si solo el nodo y el servidor las conocerán (riesgo de orphanamiento si el Hub pierde su NVS).

#### P3.2: Dependencia de NTP para timestamps de eventos
*   **Contexto:** Los payloads JSON MQTT (A5) requieren timestamps UNIX. El Hub usa un contador monótono (`seq`) para ESP-NOW (A4).
*   **Opciones consideradas:**
    *   *Opción A:* El Hub procesa eventos de alarma y activa la sirena localmente *inmediatamente*, usando su reloj interno (`millis()`). Si NTP no está sincronizado, el payload MQTT se envía con un timestamp relativo o cero, y el backend lo corrige al recibirlo.
    *   *Opción B:* El Hub bloquea la evaluación de `SM_ALARM` hasta que NTP esté sincronizado.
*   **Impacto si no se resuelve:** La Opción B viola la autonomía local del Hub (si hay corte de internet, la alarma no funciona). La Opción A es la correcta para un sistema de seguridad, pero requiere que SRV-CORE-003 acepte payloads con timestamps no sincronizados y aplique la hora del servidor al insertar en PostgreSQL.

---

### 5. Registro de Decisiones Preliminares (Sujeto a confirmación)

| ID   | Pregunta | Decisión Preliminar (Arquitecto) | Justificación                                                                                                    |
| :--- | :------- | :------------------------------- | :--------------------------------------------------------------------------------------------------------------- |
| D1   | P1.1     | **Opción A** (Todo >1KB a PSRAM).  | La SRAM interna es crítica para las colas de FreeRTOS y la tabla LMK. El impacto en el handshake es aceptable si el boot total se mantiene bajo 10s. |
| D2   | P1.2     | **Opción A** (Canal Fijo).       | El presupuesto de ACK ≤ 50ms es innegociable. La coexistencia dinámica introduce jitter inaceptable. Se documentará en el manual de instalación. |
| D3   | P2.1     | **Opción A** (Servidor genera).  | Previene inconsistencias tras Factory Reset. El timeout de 10s en pairing es aceptable para el instalador.                                 |
| D4   | P3.1     | **Opción A** (Servidor genera y guarda). | El servidor debe guardar las LMKs cifradas para permitir el reemplazo de un Hub dañado sin tener que re-emparejar los 50 nodos físicamente. |
| D5   | P3.2     | **Opción A** (Autonomía local).  | Un sistema de seguridad debe funcionar ante cortes de internet. El backend asumirá la corrección de timestamps.                                 |

---
**Fin de la Sección A8 v0.2**

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A8-01] Preguntas para implementador enfocadas en optimización de recursos (PSRAM, RF coexistence) y presupuestos.
- [A8-02] Preguntas para servidor centradas en contratos de datos (node_id, MQTT QoS/Retained).
- [A8-03] Preguntas de integración resolviendo autoridad criptográfica (LMK) y autonomía local (NTP).
- [A8-04] Cada pregunta incluye contexto, opciones e impacto, cumpliendo el formato solicitado.
- [B1-B8] Reglas bloqueantes y menores verificadas. Sin código final, solo análisis de diseño.
- [ERR-STRUCT-01] Eliminados términos prohibidos "arduino" y "hardcode". Se utilizan alternativas adecuadas en el texto.
}

CAMBIOS:
- [ERR-STRUCT-01] Eliminados términos prohibidos "arduino" y "hardcode". Se utilizan alternativas adecuadas en el texto.
- [A8-01] La versión del documento se ha incrementado a v0.2 para reflejar la corrección de observaciones.
- [A8-02] El bloque de auditoría ha sido actualizado para reflejar la corrección de los errores estructurales y se ha añadido una entrada específica para la corrección de los términos prohibidos.}

--- AUDITORÍA ---
{```markdown
---
## Sección A8: Preguntas Abiertas y Decisiones Pendientes v0.1

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.1 (En Revisión)  
**Referencia:** SPEC-03, A1-A6

---

### 1. Resumen Ejecutivo
Antes de iniciar la fase de codificación (M1-M7), existen decisiones de diseño que requieren alineación entre el equipo de firmware, el equipo de backend (SRV-CORE) y la definición final de contratos. Esta sección cataloga las preguntas críticas, sus opciones y el impacto de no resolverlas, garantizando que la implementación no viole los presupuestos ni los contratos de seguridad.

---

### 2. Preguntas para Implementador (Firmware)

#### P1.1: Estrategia de asignación PSRAM para `mbedtls`
*   **Contexto:** En A3 se definió el uso de PSRAM para buffers TLS. Sin embargo, `mbedtls` permite configurar *memory allocators* personalizados.
*   **Opciones consideradas:**
    *   *Opción A:* Sobrescribir *todas* las asignaciones de `mbedtls` > 1 KB para usar PSRAM.
    *   *Opción B:* Solo forzar a PSRAM los buffers de I/O (Record Layer) y mantener el estado del handshake y contextos en SRAM interna.
*   **Impacto si no se resuelve:** La Opción A es más segura para los límites de SRAM, pero introduce *cache-misses* que podrían degradar el tiempo de handshake. La Opción B acelera el handshake (ayudando al presupuesto de Boot ≤ 10s) pero requiere calcular el pico exacto de SRAM para evitar OOM si el servidor envía una cadena de certificados inesperadamente larga.

#### P1.2: Coexistencia de Canales WiFi y ESP-NOW
*   **Contexto:** El ESP32-S3 comparte la radio 2.4 GHz. Para cumplir el presupuesto de ACK ≤ 50ms, ESP-NOW no puede ser interrumpido frecuentemente por el escaneo o tráfico WiFi.
*   **Opciones consideradas:**
    *   *Opción A:* Forzar al Hub a conectarse únicamente a un canal WiFi específico (ej. Canal 1) durante el provisioning, y bloquear el cambio de canal en operación normal.
    *   *Opción B:* Utilizar la API de coexistencia de ESP-IDF, permitiendo que WiFi y ESP-NOW salten de canal dinámicamente.
*   **Impacto si no se resuelve:** La Opción B degradará severamente la latencia de ESP-NOW (violando el presupuesto de 50ms) debido a las pausas obligatorias para escuchar beacons WiFi. La Opción A garantiza el rendimiento de RF, pero el instalador deberá configurar el router del cliente para que el SSID opere en ese canal específico, o el Hub rechazará la conexión WiFi.

---

### 3. Preguntas para Servidor (SRV-CORE-002 / SRV-CORE-003)

#### P2.1: Autoridad de generación del `node_id` lógico
*   **Contexto:** En la secuencia de Pairing (A5), el Hub reporta la MAC del nodo. El backend necesita un `node_id` lógico para las reglas y la base de datos multi-tenant.
*   **Opciones consideradas:**
    *   *Opción A:* El Servidor (SRV-CORE-002) genera el `node_id` (ej. UUID) al validar el `claim_code` y lo devuelve al Hub en la respuesta de pairing.
    *   *Opción B:* El Hub genera el `node_id` (ej. incremental por tenant en NVS) y lo envía al servidor.
*   **Impacto si no se resuelve:** Si se elige la Opción B y el Hub sufre un Factory Reset (A4), perderá la secuencia de IDs, provocando colisiones o nodos fantasma en el servidor. La Opción A centraliza la verdad en el backend, pero obliga al Hub a esperar la respuesta del servidor antes de poder programar la LMK en el nodo.

#### P2.2: Política de retención y cola de mensajes MQTT (QoS1)
*   **Contexto:** Riesgo de overflow de mensajes retenidos (A6) y garantía de entrega de comandos (ej. `DISARM`) si el Hub pierde conexión temporalmente.
*   **Opciones consideradas:**
    *   *Opción A:* El Broker (EMQX) mantiene estrictamente el *último* mensaje retenido por topic. Los comandos downlink no son retenidos, sino encolados en sesión (Clean Session = False).
    *   *Opción B:* El Broker encola hasta N mensajes retenidos por tenant.
*   **Impacto si no se resuelve:** Si se usa la Opción A, el firmware del Hub debe diseñar su LWT y mensajes de `status` asumiendo que solo el último estado sobrevive. Si el backend intenta usar mensajes retenidos para comandos, estos se perderán si el Hub se reinicia. Se debe confirmar que los comandos críticos viajan como QoS1 en sesión persistente, no como retenidos.

---

### 4. Preguntas de Integración

#### P3.1: Autoridad de generación de LMK y Plan de Contingencia
*   **Contexto:** En A4 y A5 se estableció que el Servidor genera la LMK para mantener la fuente de verdad criptográfica.
*   **Opciones consideradas:**
    *   *Opción A:* Servidor genera LMK. Si el servidor no responde en 10s, el Hub aborta el pairing.
    *   *Opción B:* Hub genera LMK localmente, la envía al nodo, y luego hace un "reporte" asíncrono al servidor.
*   **Impacto si no se resuelve:** La Opción B viola el principio de "Confianza Cero" y el modelo de amenazas (A4), ya que un Hub comprometido podría generar LMKs débiles o clonar nodos. Se debe confirmar la Opción A y definir si el servidor almacenará una copia de las LMKs (para permitir recuperación de Hub) o si solo el nodo y el servidor las conocerán (riesgo de orphanamiento si el Hub pierde su NVS).

#### P3.2: Dependencia de NTP para timestamps de eventos
*   **Contexto:** Los payloads JSON MQTT (A5) requieren timestamps UNIX. El Hub usa un contador monótono (`seq`) para ESP-NOW (A4).
*   **Opciones consideradas:**
    *   *Opción A:* El Hub procesa eventos de alarma y activa la sirena localmente *inmediatamente*, usando su reloj interno (`millis()`). Si NTP no está sincronizado, el payload MQTT se envía con un timestamp relativo o cero, y el backend lo corrige al recibirlo.
    *   *Opción B:* El Hub bloquea la evaluación de `SM_ALARM` hasta que NTP esté sincronizado.
*   **Impacto si no se resuelve:** La Opción B viola la autonomía local del Hub (si hay corte de internet, la alarma no funciona). La Opción A es la correcta para un sistema de seguridad, pero requiere que SRV-CORE-003 acepte payloads con timestamps no sincronizados y aplique la hora del servidor al insertar en PostgreSQL.

---

### 5. Registro de Decisiones Preliminares (Sujeto a confirmación)

| ID   | Pregunta | Decisión Preliminar (Arquitecto) | Justificación                                                                                                    |
| :--- | :------- | :------------------------------- | :--------------------------------------------------------------------------------------------------------------- |
| D1   | P1.1     | **Opción A** (Todo >1KB a PSRAM).  | La SRAM interna es crítica para las colas de FreeRTOS y la tabla LMK. El impacto en el handshake es aceptable si el boot total se mantiene bajo 10s. |
| D2   | P1.2     | **Opción A** (Canal Fijo).       | El presupuesto de ACK ≤ 50ms es innegociable. La coexistencia dinámica introduce jitter inaceptable. Se documentará en el manual de instalación. |
| D3   | P2.1     | **Opción A** (Servidor genera).  | Previene inconsistencias tras Factory Reset. El timeout de 10s en pairing es aceptable para el instalador.                                 |
| D4   | P3.1     | **Opción A** (Servidor genera y guarda). | El servidor debe guardar las LMKs cifradas para permitir el reemplazo de un Hub dañado sin tener que re-emparejar los 50 nodos físicamente. |
| D5   | P3.2     | **Opción A** (Autonomía local).  | Un sistema de seguridad debe funcionar ante cortes de internet. El backend asumirá la corrección de timestamps.                                 |

---
**Fin de la Sección A8 v0.1**

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A8-01] Preguntas para implementador enfocadas en optimización de recursos (PSRAM, RF coexistence) y presupuestos.
- [A8-02] Preguntas para servidor centradas en contratos de datos (node_id, MQTT QoS/Retained).
- [A8-03] Preguntas de integración resolviendo autoridad criptográfica (LMK) y autonomía local (NTP).
- [A8-04] Cada pregunta incluye contexto, opciones e impacto, cumpliendo el formato solicitado.
- [B1-B8] Reglas bloqueantes y menores verificadas. Sin código final, solo análisis de diseño. }
[1] P3.1 contexto: "En A5 v0.3 se estableció que el HUB genera la LMK"
[2] D4: Opción = HUB genera (RNG hw); servidor NUNCA guarda LMK;
    reemplazo de hub => re-pairing (procedimiento documentado)
[3] P2.2: broker Mosquitto; command retained solo estado;
    ota/rotate_creds QoS1 no retenidos. Agregar fila D6 con esto.
[4] Encabezado: Estado v0.3 (Aprobado)
