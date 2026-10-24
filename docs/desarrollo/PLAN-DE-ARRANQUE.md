# Plan de arranque del desarrollo del firmware

**Proyecto:** Seguimiento 360 — firmware del hub ESP32-S3 y nodos  
**Versión:** 1.0  
**Fecha:** 22 de octubre de 2026  
**Punto de partida:** rama `clean-main`, snapshot documental del 2026-10-22  
**Objetivo:** iniciar M1 y M2 sin rehacer lo que ya existe y sin volver a discutir decisiones cerradas.

## 1. Regla de oro

No se repite trabajo. Este plan parte de los siguientes activos ya existentes:

- Documentación consolidada en `docs/`, con marco teórico del firmware y contratos.
- API de autenticación funcionando en Node-RED.
- Hub con WiFi, MQTT/WSS y topics multi-tenant.
- Componente `espnow_link` con frame v1, CRC16 y anti-replay.
- Esquema PostgreSQL y PWA existentes.
- Scripts de operación y pruebas manuales.

Lo que falta no es “empezar de cero”: es **cerrar brechas concretas** y demostrarlas con las pruebas de aceptación.

## 2. Estado actual

| Área | Estado | Evidencia |
|---|---|---|
| Documentación | Consolidada | `docs/README.md`, `docs/marco-teorico/marco-teorico-firmware.md` |
| Autenticación JWT | Implementada | `backend/nodered-flows/flow-auth.json` |
| Hub WiFi + MQTT/WSS | Implementado | `firmware/hub/main/main.c`, `components/mqtt_link` |
| Topics multi-tenant | Implementado | `get_mqtt_topic()` en `main.c` |
| Frame ESP-NOW v1 | Componente implementado | `components/espnow_link` |
| Integración del frame v1 en `main.c` | Parcial | `main.c` aún usa `security_payload_t` |
| Nodo ultrasónico | Prototipo con payload de 7 bytes | `firmware/sensor-ultrasonic/main/main.c` |
| NVS de configuración y alarma | Parcial | SPEC-01 HUB-F-010 |
| Tabla de nodos y `node_lost` | Pendiente | SPEC-01 HUB-F-008 |
| Provisioning SoftAP + claim | Parcial | `components/sm_prov`, `web_config` |
| OTA con firma y rollback | Parcial | `main/ota_manager.c` |
| MQTT autenticado + ACL | Pendiente | `INFRA-REAL`, SPEC-01 |
| Pruebas AT-001 a AT-010 | Pendientes | SPEC-03 |
| CI de firmware | No existe | Solo hay CI de documentación |

## 3. Brechas P0

1. **Contrato del payload v1.** Definir el payload exacto por tipo de nodo y actualizar la SPEC-02.
2. **Migrar el nodo ultrasónico al frame v1.** Hoy envía 7 bytes crudos; el hub espera cabecera de 19 bytes y CRC16.
3. **Integrar `espnow_link` en `main.c`.** Eliminar la ruta legacy y usar una sola ruta de recepción.
4. **Persistir configuración y estado en NVS.** Tenant, site, credenciales, estado de alarma y secuencias.
5. **Tabla de nodos y `node_lost`.** Última actividad, batería y 3 heartbeats perdidos.
6. **Autenticar MQTT y aplicar ACL.** Dejar `allow_anonymous true`.
7. **Instrumentar y ejecutar AT-001, AT-002, AT-003 y AT-005.**

## 4. Backlog priorizado

| Tarea | Archivos principales | Criterio de aceptación | Días | Depende de |
|---|---|---|---:|---|
| **FW-001** — Cerrar payload v1 por tipo de nodo | `docs/firmware/specs/02-interfaces.md`, `shared/espnow_protocol/` | ✅ Hecho (2026-10-23) | 1 | — |
| **FW-002** — Migrar nodo ultrasónico a frame v1 | `firmware/sensor-ultrasonic/main/main.c` | ✅ Hecho (2026-10-23) | 2 | FW-001 |
| **FW-003** — Integrar el frame v1 en el hub | `firmware/hub/main/main.c` | ✅ Hecho (2026-10-23) | 2 | FW-002 |
| **FW-004** — Instrumentar ACK y RX→publish | `espnow_link.c`, `main.c` | ✅ Hecho (2026-10-23): ACK 14 ms, publish 46-92 ms | 1 | FW-003 |
| **FW-005** — NVS: configuración y estado | `main.c`, `components/hub_core` | Reinicio conserva alarma y tenant | 2 | FW-003 |
| **FW-006** — Tabla de nodos y `node_lost` | `main.c`, `t_node_mgr` | 3 heartbeats perdidos → evento | 2 | FW-005 |
| **FW-007** — MQTT auth + ACL | `mosquitto.conf`, `passwd`, `acl` | Anónimo rechazado; dispositivo solo su tenant | 2 | FW-003 |
| **FW-008** — Provisioning claim completo | `components/sm_prov`, `web_config` | Hub sin config → SoftAP → claim → active | 3 | FW-005 |
| **FW-011** — Pairing del nodo con el hub | `sensor-ultrasonic/main/pairing.{c,h}` | ✅ Hecho (2026-10-23) | 2 | FW-003 |
| **FW-009** — OTA con firma y rollback | `ota_manager.c` | ✅ Hecho (2026-10-24): AT-015/016/017 | 3 | FW-007 |
| **FW-010** — CI de firmware | `.github/workflows/firmware.yml` | ✅ Hecho (2026-10-23): build de hub y nodo en cada PR | 2 | FW-003 |

## 5. Sprint 1: próximos 10 días

### Día 1–2 — Contrato y payload

- [ ] Cerrar FW-001 con el equipo de backend.
- [ ] Actualizar `docs/firmware/specs/02-interfaces.md`.
- [ ] Añadir el payload al capítulo 6 del marco teórico si cambia.
- [ ] Congelar el frame: no se cambia sin *change request*.

### Día 3–5 — Nodo y hub

- [ ] FW-002: migrar `sensor-ultrasonic` al frame v1.
- [ ] FW-003: integrar `espnow_link` en `main.c`.
- [ ] FW-004: instrumentar ACK y RX→publish.
- [ ] Medir con analizador lógico: ACK ≤ 50 ms.

### Día 6–7 — Persistencia y nodos

- [ ] FW-005: NVS de configuración, alarma y secuencias.
- [ ] FW-006: tabla de nodos y `node_lost`.
- [ ] Probar reinicio con alarma armada: debe conservarse.

### Día 8 — MQTT seguro

- [ ] FW-007: `allow_anonymous false`, `password_file`, `acl_file`.
- [ ] Crear usuario `device_<serial>` y probar aislamiento entre tenants.
- [ ] Verificar que un dispositivo no puede publicar en otro tenant.

### Día 9 — Pruebas

- [ ] Ejecutar AT-001, AT-002, AT-003 y AT-005.
- [ ] Registrar fecha, firmware y evidencia.
- [ ] Corregir regresiones.

### Día 10 — Revisión

- [ ] Revisar FW-001 a FW-007.
- [ ] Actualizar SPEC-03 con resultados.
- [ ] Decidir si se avanza a M4 o se estabiliza.

## 6. Comandos de trabajo

### Hub

```bash
. $IDF_PATH/export.sh
cd firmware/hub
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Nodo ultrasónico

```bash
. $IDF_PATH/export.sh
cd firmware/sensor-ultrasonic
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Broker

```bash
docker exec mosquitto mosquitto_sub -h localhost -t 'seguimiento360/#' -v
```

### API de usuarios

```bash
API="http://localhost:1880"

curl -sS -X POST "$API/api/auth/register" \
  -H "Content-Type: application/json" \
  -d '{"email":"prueba@example.com","password":"PruebaSegura123","full_name":"Prueba"}' | jq

TOKEN=$(curl -sS -X POST "$API/api/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"email":"prueba@example.com","password":"PruebaSegura123"}' | jq -r '.token')

curl -sS "$API/api/auth/profile" -H "Authorization: Bearer $TOKEN" | jq
```

Detalle completo en [API de autenticación](../backend/api-auth.md).

### Documentación

```bash
npx --yes markdownlint-cli2 "**/*.md"
bash scripts/check-docs.sh
mkdocs build --strict
```

## 7. Decisiones cerradas

| Tema | Decisión | No volver a discutir |
|---|---|---|
| LMK | La genera el hub; el servidor no la recibe | Sí |
| Frame | v1 con magic, cabecera de 19 bytes y CRC16 | Sí |
| MQTT | WSS + CA + usuario/password por dispositivo | Sí, hasta mTLS |
| Provisioning | SoftAP + portal cautivo | Sí |
| Núcleo de `t_logic` | Core 1 | Sí |
| Flash/PSRAM | 16 MB / 8 MB | Sí |
| Documentación | `docs/` es la única fuente | Sí |

## 8. Preguntas abiertas

| ID | Pregunta | Dueño | Fecha límite |
|---|---|---|---|
| Q1 | Payload v1 exacto por tipo de nodo | Firmware | Día 2 |
| Q2 | ¿Armado parcial en fase 1? | Producto | Día 5 |
| Q3 | ¿PMK por tenant o global? | Arquitectura | Día 10 |
| Q4 | ¿Quién genera el `node_id` lógico? | Backend | Día 5 |
| Q5 | ¿Comandos críticos por sesión persistente? | Backend | Día 8 |
| Q6 | Custodia de la clave de firma OTA | Operaciones | Antes de M6 |
| Q7 | Timestamp local si NTP no sincroniza | Backend | Día 8 |

## 9. Definición de terminado

Una tarea se cierra cuando:

1. Compila para la plataforma objetivo.
2. Tiene prueba o AT asociada.
3. Se midió el presupuesto afectado.
4. Se actualizaron SPEC y documentación.
5. No hay secretos ni artefactos en el commit.
6. Otro ingeniero la revisó.

## 10. Activos que no se deben rehacer

| Activo | Ruta | Uso |
|---|---|---|
| Marco teórico del firmware | `docs/marco-teorico/marco-teorico-firmware.md` | Referencia de arquitectura |
| API de usuarios | `docs/backend/api-auth.md` | Contrato y pruebas |
| Flow de autenticación | `backend/nodered-flows/flow-auth.json` | Registro, login, perfil |
| Esquema de base de datos | `backend/database/schema.sql` | Tablas existentes |
| PWA | `frontend/pwa/` | Cliente de la API |
| Componente ESP-NOW | `firmware/hub/components/espnow_link` | Frame v1 |
| Scripts de operación | `scripts/` | Build, backup, KB |
| Pruebas manuales | `tests/test_suite.sh` | Punto de partida de AT |

## 11. Criterios de salida de M1–M3

### M1 — BSP y NVS

- 10.000 ciclos de escritura NVS sin corrupción.
- `power_loss` y `tamper` generan eventos.
- Estado de alarma sobrevive reinicio.

### M2 — ESP-NOW v1

- 1.000 frames/s sin pérdida.
- ACK < 50 ms medido.
- Nodo y hub usan el mismo frame.

### M3 — WiFi/MQTT

- Boot a MQTT ≤ 10 s.
- LWT y reconexión con backoff.
- MQTT autenticado y ACL por tenant.

## 12. Referencias

- [Marco teórico del firmware](../marco-teorico/marco-teorico-firmware.md)
- [API de autenticación](../backend/api-auth.md)
- [Requisitos](../firmware/specs/01-requisitos.md)
- [Contratos](../firmware/specs/02-interfaces.md)
- [Pruebas de aceptación](../firmware/specs/03-pruebas-aceptacion.md)
- [Estado por fases](../fases/RESUMEN_FASES.md)

## Tareas abiertas de OTA

**FW-012 — OTA del nodo.** Hecho (2026-10-24), verificado en AT-018. El nodo
tiene tabla de particiones propia con `ota_0`/`ota_1`, recibe del hub las
credenciales WiFi y la URL por ESP-NOW, descarga por HTTPS, verifica la firma
RSA-3072 y confirma el rollback al arrancar. La firma esta probada en las dos
direcciones, incluida una imagen firmada con otra clave.

Sigue pendiente de decision si la clave debe rotarse por nodo o seguir siendo
unica para toda la instalacion, y conviene vigilar el margen de particion: el
binario ocupa el 94% de `ota_0`.

**Firma del bootloader.** La firma actual cubre la app, no el bootloader. Un
atacante con acceso fisico podria reemplazarlo y saltarse la verificacion.
Cerrarlo exige Secure Boot por hardware, que quema eFuses y es irreversible:
hay que decidirlo con cuidado.

## Estado tras la sesion del 2026-10-24

**Hecho y verificado en esta sesion**

| Tarea | AT | Como se verifico |
|---|---|---|
| HUB-NF-005 backoff (nodo) | AT-010 | Espera creciente medida: 55 s, 117 s, 473 s |
| HUB-F-011 provisioning condicional | AT-019 | SoftAP solo sin configuracion, en ambos estados |
| HUB-F-016 claim code en el portal | - | 11 casos unitarios, 5 codigos reales de la base |

**Hecho pero sin verificar**

- **HUB-NF-005 en el hub.** La tarea de reconexion con backoff esta
  implementada, pero no se ha podido forzar una caida de su enlace: el
  servidor no tiene interfaz WiFi, el router no expone administracion y no hay
  permisos para bloquear trafico. Hace falta un banco donde se pueda apagar el
  AP o el router.

**Pendiente con trabajo real**

- **HUB-F-016: el claim code no existe en produccion.** Aqui decia que el
  backend estaba completo y solo faltaba alinear los seriales. **Era falso**:
  se habia analizado la copia del repo, no el flujo desplegado. Produccion
  (117 nodos) **no tiene** `/api/claim` ni `/api/admin/generate-codes`; esos
  endpoints solo existen en `backend/nodered-flows/flows.json`, que no es lo
  que corre.

  Antes de retomar esto hay que **decidir si el claim code debe existir**. Si
  el alta de hubs se hace hoy de otra forma, el requisito cambia de alcance.
  Ver `docs/firmware/HALLAZGO-claim-code.md` y
  `docs/firmware/change-requests/CR-001-serial-mac.md`.

- **La brecha de seriales ya no aplica.** El CR-001 comprobo que produccion
  usa la MAC como identificador en todas sus consultas. Lo que faltaba era el
  contrato escrito, y ya esta en HUB-IF-013.
- **AT-009 operacion 24/7.** Requiere un mes de calendario. El watchdog esta
  activo; conviene dejar un registro de uptime y revisarlo despues.
- **Unificar los dos sistemas de configuracion.** `sm_prov` tiene su FSM con
  `nvs_manager_has_credentials()`, y el arranque lo gobierna `active_config`.
  `sm_prov` sigue sin invocarse desde `main.c`.

**Herramientas nuevas**

- `tests/firmware/` compila y ejecuta logica de firmware en el PC. La prueba
  extrae la funcion de `main.c` para no validar una copia.
- La configuracion del hub se respalda copiando tres particiones
  (`nvs`, `nvs_state`, `nvs_nodes`); probado borrando y restaurando.
