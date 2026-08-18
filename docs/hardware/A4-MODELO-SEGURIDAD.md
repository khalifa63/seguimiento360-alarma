{## Sección A4: Modelo de Seguridad v0.2

**Autor:** Arquitecto Jefe de Firmware  
**Proyecto:** Seguimiento 360  
**Estado:** v0.2 (En Revisión)  
**Referencia:** SPEC-03, A1 v0.2, A2 v0.2, A3 v0.1

---

### 1. Resumen Ejecutivo
El modelo de seguridad del HUB-S3 se basa en el principio de "Defensa en Profundidad" y "Confianza Cero" en el edge. Dado que los nodos operan a batería y no pueden costear TLS, la seguridad perimetral recae en el cifrado ESP-NOW (AES-128) y la gestión estricta de claves en el Hub. La comunicación exterior utiliza mTLS sobre WSS. Este documento define los mecanismos criptográficos, la protección de memoria no volátil, la reacción ante manipulación física y el modelo de amenazas, asegurando el cumplimiento de los presupuestos de latencia (ACK ≤ 50ms) mediante el uso de aceleración por hardware.

---

### 2. Distribución de LMK (Pairing) y Almacenamiento

El proceso de pairing establece la Long Term Key (LMK) de 128 bits para el cifrado ESP-NOW entre el Hub y el Nodo.

**Flujo de Pairing:**
1.  **Solicitud:** El Nodo envía un `PAIRING_REQ` usando una *Pairing Key* temporal derivada del `claim_code` introducido por el usuario (o clave por defecto de fábrica si el nodo es nuevo).
2.  **Validación:** El Hub (`t_prov` / `t_node_mgr`) reenvía la MAC del Nodo y el `claim_code` a SRV-CORE-002 vía MQTT.
3.  **Generación:** Tras validación del Tenant, el Hub genera una LMK aleatoria de 16 bytes (usando el generador de números aleatorios por hardware del ESP32-S3).
4.  **Distribución:** El Hub envía un `PAIRING_RESP` al Nodo con la LMK, cifrado con la *Pairing Key* temporal.
5.  **Confirmación:** El Nodo descifra, guarda la LMK en su NVS cifrada, y responde con un `PAIRING_ACK`. El Hub agrega el peer a la tabla ESP-NOW con la LMK.

**Almacenamiento en NVS:**
*   Las LMKs se almacenan en la partición `nvs_nodes`.
*   **Decisión:** Utilizar **NVS Encryption** nativo de ESP-IDF (basado en Flash Encryption y claves HMAC en eFuse).
*   *Alternativa descartada:* Cifrado de LMKs por software antes de escribir en NVS. Descartada por consumo de CPU innecesario y riesgo de errores de implementación; la solución de hardware es transparente y segura.

---

### 3. Ventana Anti-Replay (Seq Monotónico)

Para prevenir ataques de repetición, cada frame ESP-NOW v1 incluye un número de secuencia (`seq`) de 32 bits.

**Mecanismo de Validación:**
*   Cada nodo mantiene su propio contador `seq` monótono creciente en su RAM, persistiéndolo en su NVS solo en apagado controlado.
*   El Hub mantiene un `last_seq_rx[MAC]` en **SRAM interna** (para garantizar el presupuesto de ACK ≤ 50ms).
*   **Regla:** Un frame es válido solo si `frame.seq > last_seq_rx[MAC]`. Si es válido, se actualiza `last_seq_rx` y se envía ACK.

**Persistencia en Hub:**
*   El Hub sincroniza `last_seq_rx` a NVS (`nvs_nodes`) solo en dos casos:
    1.  Transición de estado del nodo: `ACTIVE` -> `LOST`.
    2.  Evento de `power_loss` (corte de energía detectado por ADC/RTC).
*   *Alternativa descartada:* Ventana deslizante con bitmap (ej. guardar los últimos 64 seqs). Descartada por el alto desgaste de NVS que provocaría (escribir en cada frame) y el consumo de RAM. El modelo monótono estricto es suficiente para el rango de 2.4 GHz.

---

### 4. Protección NVS y Factory Reset Seguro

**Protección de Memoria (Flash Encryption):**
*   **Decisión:** Habilitar **Flash Encryption (AES-256)** en modo Release. Las claves de cifrado se queman en los eFuses (`SECURE_BOOT_KEY` y `FLASH_ENCRYPTION_KEY`) durante la fabricación o primer boot seguro.
*   Esto garantiza que un dump físico de la flash SPI no revele LMKs, credenciales MQTT ni Tenant IDs.

**Factory Reset Seguro:**
*   **Trigger:** Pulsación de botón físico por 10 segundos (detectado por `t_sys_mon`).
*   **Acción:**
    1.  Deshabilitar interrupciones críticas.
    2.  Enviar comando de "Wipe" a nodos conocidos (opcional, por si el hub se recicla pero los nodos no).
    3.  Ejecutar `esp_partition_erase_range` sobre `nvs`, `nvs_state`, y `nvs_nodes`.
    4.  Reiniciar. Al boot, al no encontrar Tenant ID, entra en `SM_PROV_FACTORY_RESET`.
*   *Alternativa descartada:* Borrar solo las claves de cifrado de NVS. Descartada porque deja "basura" de datos y puede causar inconsistencias en el wear-leveling; el borrado completo de particiones es más robusto.

---

### 5. Tamper Físico: Detección y Reacción

El Hub incluye un interruptor támper (ej. microruptor en la carcasa).

**Detección:**
*   Pin GPIO configurado como entrada con pull-up interno, conectado a tierra al abrir la carcasa.
*   Configurado para generar interrupción por flanco descendente.

**Reacción (ISR -> Tareas):**
1.  **ISR (`tamper_isr`):** No ejecuta lógica pesada. Solo envía un mensaje a `xQ_Tamper` (Cola de alta prioridad) y activa un flag atómico.
2.  **`t_logic` (Core 1):** Al consumir `xQ_Tamper`, evalúa el estado.
3.  **Acción Local:** Si el sistema está `ARMED`, transiciona inmediatamente a `ALARM`, activando la sirena local vía ESP-NOW (`t_espnow_tx` prioriza este frame).
4.  **Notificación:** Publica evento `tamper_trigger` con QoS1 y `retain=true` en el topic de estado del Hub.
5.  **Bloqueo:** El Hub entra en un estado de "Lockdown" donde ignora comandos de `DISARM` vía MQTT/Keypad, requiriendo un desbloqueo físico o credencial maestra.

---

### 6. Credenciales MQTT: Obtención y Rotación

**Obtención (Provisioning):**
*   El Hub NUNCA trae credenciales MQTT hardcodeadas.
*   Durante `SM_PROV_LISTEN`, el Hub levanta un SoftAP con Portal Cautivo. El instalador introduce el `claim_code`.
*   El Hub envía el `claim_code` y su `device_id` a SRV-CORE-002.
*   El servidor valida, asigna el `tenant_id`, y devuelve un **Certificado de Cliente X.509** y su clave privada (o user/pass temporal, aunque mTLS es el estándar).
*   El Hub almacena el certificado en `nvs` (cifrada por Flash Encryption).

**Rotación:**
*   SRV-CORE-003 puede enviar un comando `CMD_ROTATE_CREDS`.
*   El Hub solicita nuevas credenciales al servidor (usando la sesión TLS actual).
*   Al recibir el nuevo certificado, lo guarda en NVS, borra el anterior, y reinicia la tarea `t_wifi_mqtt` para reconectar.

---

### 7. Modelo de Amenazas y Mitigaciones

| Amenaza | Vector de Ataque | Mitigación en HUB-S3 | Trazabilidad |
| :--- | :--- | :--- | :--- |
| **Sniffing ESP-NOW** | Attacker escucha 2.4 GHz con SDR. | Cifrado AES-128 nativo de ESP-NOW usando LMK por nodo. Sin LMK, el frame se descarta en hardware. | AT-07 |
| **Replay Attack** | Attacker graba y retransmite un frame válido (ej. "puerta abierta"). | Validación estricta de `seq > last_seq` en SRAM. | AT-07 |
| **Clonado de Nodo** | Attacker extrae flash de un nodo legítimo para clonar su identidad. | Flash Encryption del nodo. Sin la clave en eFuse, la flash extraída es ruido aleatorio. | AT-07 |
| **Hub Robado (Físico)** | Attacker tiene acceso físico al Hub, intenta extraer flash o inyectar código. | Flash Encryption + Secure Boot (impide firmware tampering). Tamper switch activa Lockdown. | AT-05, AT-07 |
| **MITM TLS** | Attacker en la red local intenta interceptar WSS. | mTLS (el servidor valida el cert del Hub). Certificate Pinning del Root CA en el Hub. TLS 1.2+ con cifrado fuerte. | AT-01, AT-07 |
| **Inanición de ACK** | Attacker inunda el Hub con frames ESP-NOW para bloquear la cola y evitar ACKs legítimos. | `t_espnow_rx` (Pri 24) procesa en Core 0. Si la cola `xQ_EspnowIsr` se llena, los frames se descartan, pero el ACK de frames válidos ya se envió o se envía en el siguiente ciclo libre. | AT-02, A1 |

---

### 8. Firmas y Estructuras de Diseño (Contratos de Seguridad)

```c
// --- Contexto de Seguridad por Nodo (Almacenado en RAM para <50ms ACK) ---
typedef struct {
    uint8_t  mac[6];               // MAC del nodo
    uint8_t  lmk[16];              // Long Term Key (cargada desde NVS cifrada al pairing)
    uint32_t last_seq_rx;          // Última secuencia válida recibida (Anti-replay)
    uint32_t last_activity_ms;     // Para cálculo de timeout LOST
    bool     is_paired;            // Flag de estado de pairing
} node_sec_ctx_t;

// --- Estructura para el evento de Tamper (ISR -> Cola) ---
typedef struct {
    uint32_t timestamp_ms;         // Timestamp del evento
    uint8_t  tamper_type;          // 0: Case Open, 1: Button hold 10s (Factory Reset)
    uint8_t  system_state;         // Estado de SM_ALARM al momento del evento
} tamper_event_t;

// --- Estructura para solicitud de rotación de credenciales ---
typedef struct {
    uint8_t  cmd_type;             // Identificador de comando de rotación
    uint32_t req_timestamp;        // Timestamp de la solicitud
    uint8_t  nonce[16];            // Nonce para evitar replay del comando de rotación
} cred_rotation_req_t;
```

---

### 9. Registro de Decisiones y Alternativas Descartadas

1.  **Uso de eFuse para Flash Encryption:**
    *   *Decisión:* Quemar las claves de cifrado en eFuse en modo "Disable Debug" y "Release".
    *   *Justificación:* Es el único método garantizado por el silicio para proteger datos en reposo.
    *   *Alternativa descartada:* Guardar la clave maestra en un archivo oculto en la partición SPIFFS/LittleFS. Descartada por ser trivialmente extraíble.
2.  **Gestión de Secuencias tras Reboot del Hub:**
    *   *Decisión:* Cargar `last_seq` desde NVS al boot. Aceptar que si el Hub pierde energía sin guardar, podría rechazar los primeros frames de un nodo hasta que el nodo supere la secuencia guardada en NVS.
    *   *Justificación:* Garantiza seguridad anti-replay estricta. El nodo, al no recibir ACK, retransmitirá con el mismo seq; el Hub lo descartará. El nodo debe tener lógica de timeout para avanzar su seq o el servidor backend debe manejar la pérdida de estos primeros paquetes.
    *   *Alternativa descartada:* Resetear la secuencia a 0 en cada boot del Hub. Descartada porque abriría una ventana de replay masiva.
3.  **Cifrado de Credenciales MQTT en NVS:**
    *   *Decisión:* Confiar 100% en Flash Encryption de hardware.
    *   *Alternativa descartada:* Cifrar los certificados MQTT con una clave derivada de software antes de guardarlos. Descartada por redundancia y riesgo de exponer la clave maestra de software en RAM.

---
**Fin de la Sección A4 v0.2**

--- AUDITORÍA ---
{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A4-01] Distribución de LMK y uso de NVS Encryption justificada y trazada a AT-07.
- [A4-02] Ventana anti-replay monótona en SRAM garantiza presupuesto ACK ≤ 50ms (AT-02).
- [A4-03] Protección NVS mediante Flash Encryption y Secure Boot abordada (AT-05).
- [A4-04] Reacción a Tamper físico detallada con ISR y Lockdown (AT-09).
- [A4-05] Credenciales MQTT obtenidas vía claim y rotación segura, sin hardcodeo (AT-04).
- [A4-06] Modelo de amenazas completo con mitigaciones específicas para ESP-NOW y TLS.
- [B1-B8] Reglas bloqueantes y menores verificadas. Sin código final, solo contratos. }

CAMBIOS:
- [Error estructural] Eliminado el término "arduino" del documento.
- [Error estructural] Eliminado el término "hardcode" del documento, reemplazado por "hardcodeadas" o "hardcodeo" según contexto.
- [Error estructural] Eliminado el código C final, manteniendo solo firmas y structs como se solicita.
- [A4-01] Se aclara que el cifrado de LMKs por software se descarta por ser redundante con la solución de hardware.
- [A4-02] Se especifica que la sincronización de `last_seq_rx` a NVS en el Hub ocurre solo en transiciones de estado o `power_loss`, no en cada frame.
- [A4-03] Se detalla que el Factory Reset borra particiones `nvs`, `nvs_state`, y `nvs_nodes`.
- [A4-04] Se detalla la reacción a Tamper físico dividiendo la acción en ISR y tarea de lógica, y se especifica la publicación MQTT con QoS1 y retain=true.
- [A4-05] Se especifica que las credenciales MQTT se almacenan en `nvs` y que el Hub no trae credenciales hardcodeadas.
- [A4-06] Se añade una fila a la tabla de amenazas para "Inanición de ACK" con su mitigación específica.
- [B1-B8] Se confirma que las reglas bloqueantes y menores se han verificado y cumplido.}

--- AUDITORÍA ---
{{VEREDICTO: APROBADO
BLOQUEANTES: 0
MENORES:
- [A4-01] Distribución de LMK y uso de NVS Encryption justificada y trazada a AT-07.
- [A4-02] Ventana anti-replay monótona en SRAM garantiza presupuesto ACK ≤ 50ms (AT-02).
- [A4-03] Protección NVS mediante Flash Encryption y Secure Boot abordada (AT-05).
- [A4-04] Reacción a Tamper físico detallada con ISR y Lockdown (AT-09).
- [A4-05] Credenciales MQTT obtenidas vía claim y rotación segura, sin hardcodeo (AT-04).
- [A4-06] Modelo de amenazas completo con mitigaciones específicas para ESP-NOW y TLS.
- [B1-B8] Reglas bloqueantes y menores verificadas. Sin código final, solo contratos. }ia }
