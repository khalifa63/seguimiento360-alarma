# Integración del botón de pánico — 2026-10-31

La integración del nodo **BOTÓN DE PÁNICO** quedó completa de extremo a
extremo salvo un paso que exige manos: flashear el buzzer con la semántica
`0xFFFF`. El resto — spec (SPEC-02 v0.6), BD, firmware del hub y del nodo
(ESP32-S3), toggle en Node-RED, portal, registro en BD y verificación con
AT-024..AT-028 — está entregado y verificado en producción. Este documento
deja rastro de decisiones, hallazgos y pendientes.

## Qué se hizo

| Fase | Contenido | Commit |
|---|---|---|
| 1 | SPEC-02 v0.6: `PANIC`, `seconds=0/0xFFFF`, HUB-IF-015 ampliado | `4eaf582` |
| — | Excepción v0.6: BUZZ de silencio con push media (HUB-IF-005) | `c2dcc97` |
| 2 | `device_type='boton_panico'` sin migración de esquema | `3b8ec86` |
| 3 | Hub: evento PANIC + bucle latched + paro/disarm del latch | `0e577ab` |
| — | Buzzer: handler `seconds=0xFFFF` (bucle latched) | `9f75fa9` |
| 4 | `firmware/boton-panico/`: pulsador con pairing por barrido | `2cf6f56` |
| 5 | Node-RED: toggle por MAC + despacho latch/paro + `GET /api/panico` | `3dd62c4` |
| 6 | Portal: tarjeta `boton_panico` con estado y última oprimida | `30cda3c` |
| 7 | Registro del nodo (devices id 10 + nodes id 10, serial minúsculas) | (SQL directo, sin commit) |
| 8 | AT-024..AT-028 con evidencia cruda | `test:` (este ciclo) |
| + | Buzzer flasheado y re-emparejado: AT-025 completo (bucle latched audible) | `test:` (este ciclo) |
| — | Fix: toggle lee `msg.srcMac` en raíz + salud de `boton_panico` | `3b9b842` |
| 9 | Bitácora + push | (este commit) |

## Decisiones

- **El toggle vive en Node-RED, no en el nodo.** Cada oprimida es un `PANIC`
  `value=1`; alternar en el nodo habría dejado el toggle desincronizado para
  siempre con una sola oprimida perdida. El estado (`panico_arm`) vive en el
  **contexto global** de Node-RED (un registro por MAC con `armado`, `desde`
  y la última `seq`), que es lo que lee el portal por `GET /api/panico`.
- **Dedup por seq en Node-RED** + **reenvío con la misma seq en el nodo**:
  `esp_now_send` no confirma entrega; un PANIC duplicado alternaría dos
  veces (armar+desarmar). Con la misma seq, Node-RED descarta el repetido
  (verificado: AT-026).
- **Sin cooldown**: la ruta pánico→buzzer no pasa por el cooldown de 60 s de
  los DOOR; las protecciones propias son anti-rebote 50 ms, rate-limit de
  3 s en el nodo y el dedup por seq.
- **Sin enlace no se encola**: una oprimida sin hub se registra en el log y
  no se envía (el firmware lo avisó en el arranque de AT-024).
- **`0xFFFF` sin payload nuevo**: cabe en el `uint16_t` de
  `espnow_payload_buzzer_t` (20 B, magia 0xE8). El frame v1 no cambió. La
  constante `ESPNOW_BUZZER_LATCHED` vive en el header compartido.
- **El latch no usa el ciclo de sirena**: no hay sensor que re-verificar; el
  hub recuerda en RAM (`s_latch_mac`) a quién silenciar con `disarm`.
- **Silenciar no es desarmar**: el mute del portal publica `seconds=0` y el
  toggle del botón queda armado (AT-027).

## Hallazgos

1. **Swap de puertos USB (casi deja el hub inservible).** Entre el
   emparejamiento (13:41) y el flasheo del hub (14:14), `ttyACM0` y
   `ttyACM1` se intercambiaron: la imagen del hub terminó en la placa del
   botón, que aborta en `octal_psram: PSRAM ID read error` (no tiene PSRAM)
   y quedó en bucle de arranque. Restaurado con verificación **de la MAC
   antes de cada flasheo** (`read_mac` → compárala → flashea). Regla nueva
   de operación: nunca confiar en el número de puerto para elegir firmware.
2. **DTR/RTS sostenidos por pyserial tumban el hub.** Capturar la consola
   del hub (USB-Serial-JTAG) con pyserial dejando `dtr=True, rts=False`
   varios minutos lo metió en bucle de abort (mismo `PSRAM ID read error`);
   al cerrar el lector arrancó limpio. esptool es seguro (secuencias
   cortas); lo que no se debe hacer es **sujetar el puerto** con lecturas
   largas mientras el hub opera.
3. **Reinicio de la infraestructura a las 14:04-14:07** (postgres y nodered
   se reiniciaron solos durante la sesión): la primera simulación se perdió
   (mute HTTP 000, eventos sin ingestar) y el toggle volvió a desarmado —
   validación en vivo de la limitación documentada del estado RAM: el camino
   de salida es **Silenciar**.
4. **El login resuelve el tenant por `tenants.owner_user_id`**, no por
   `users.tenant_id`: apuntar el usuario a otro tenant en la BD no cambia el
   JWT. Para el e2e se firmó un token con el `jwtSecret` del sistema
   (tenant 2) en vez de mutar el ownership del tenant real (id 9). El
   usuario e2e (`panico-e2e@seg360.test`, creado por API) y su tenant
   temporal se borraron al cierre.
5. **`HUB-IF-006` documentaba `node_cfg` con `mac`/`name`**, pero el hub
   parsea `node_mac`/`node_name` (más `distance_threshold` obligatorio): el
   primer `node_cfg` del botón se perdió y el `HEAR` salió con
   `node_name` vacío. Corregido el ejemplo de la spec y publicado con las
   claves reales (mismo patrón que el hallazgo del buzzer del 2026-10-30).
6. **`mosquitto_sub` hacia archivo bufferiza**: dos capturas salieron vacías
   por esto (`kill` antes del flush). `stdbuf -oL mosquitto_sub ...` lo
   arregla.
7. **La salud del botón mostraba "Sensor de distancia 1 cm"**: misma
   regresión que el buzzer (las ramas del HC-SR04 interpretan `value` como
   centímetros). Rama propia `boton_panico` en `Calcular salud` (`3b9b842`).
8. **El hub se cayó a las ~14:26 mientras se conectaba el buzzer** (corte de
   alimentación al mover enchufes): `HUB_OFFLINE` retenido, cero eventos
   durante ~10 min. Un `read_mac` de esptool lo reseteo y volvio limpio
   (`HUB_ONLINE`, reset_reason USB). Sintoma clave para diagnosticarlo:
   los comandos del toggle salian por MQTT pero el hub no publicaba
   `BUZZER_RESULT` ni reenviaba nada — el silencio del `diag` con el flow
   despidiendo comandos es firma de hub caido.
9. **El flasheo del buzzer borro su emparejamiento** (arranco en modo SIN
   VINCULAR, barrido con `Error al enviar: 1` en todos los canales). Re-
   emparejado abriendo la ventana de 120 s por MQTT: el barrido recibio el
   ACK y quedo vinculado en el primer ciclo. Tras actualizar el firmware de
   cualquier nodo, **verificar `Hub vinculado en NVS` y re-emparejar si hace
   falta**.

## Verificación (resumen; el detalle crudo está en SPEC-03)

| AT | Alcance | Resultado |
|---|---|---|
| AT-024 | Registro, pairing, HEAR auditable | PASS |
| AT-025 | 1.ª oprimida → toggle → `seconds=65535` → `BUZZ value=1` (bucle latched) | PASS (buzzer actualizado) |
| AT-026 | 2.ª oprimida → `seconds=0` → `BUZZ value=0` + dedup seq | PASS |
| AT-027 | Silenciar del portal con toggle armado | PASS |
| AT-028 | «Probar ESP-NOW» (rtt 65 ms) y salud del botón | PASS |

## Pendientes

- **Oprimidas físicas de cierre** (opcional, registro humano): la ruta
  ESP-NOW del botón está probada (HEAR + PONG con rtt 65 ms, AT-024/AT-028) y
  los AT-025/026 usan el evento PANIC publicado a nivel MQTT (mismo topic y
  JSON que emite el hub). Con el buzzer ya actualizado, repetir
  1.ª/2.ª oprimida con la mano cuando se monte el botón en su ubicación.
- **Alimentar el botón y el buzzer por cargador de teléfono**: ambos pasaron
  por el USB del servidor para flashear. Misma lección de brownout del REED.
  El buzzer debe volver a su cargador en la Sala (el botón aún no tiene
  ubicación definitiva).
- **Alcance**: si el botón se monta en otro piso, validar el enlace con
  «Probar ESP-NOW» antes de fijarlo (ESP-NOW no atraviesa bien varias losas).

## Referencias

- Spec: `docs/firmware/specs/02-interfaces.md` (v0.6, HUB-IF-002/005/006/015).
- Pruebas: `docs/firmware/specs/03-pruebas-aceptacion.md` (AT-024..AT-028).
- Firmware: `firmware/boton-panico/` (nodo), `firmware/hub/main/main.c`
  (PANIC + latch), `firmware/buzzer/main/main.c` (0xFFFF).
- Lecciones previas: `docs/operaciones/2026-10-30-duplicado-nodos-y-ping.md`
  y `docs/operaciones/2026-10-30-integracion-buzzer.md`.
