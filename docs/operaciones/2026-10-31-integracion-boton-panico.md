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
| + | Campo: PANI→PANIC en el hub, envio unico del boton, dedup x3, push de prueba en salud (AT-029) | `fix:` x3 + `test:` |
| + | Campo reed: cooldown 10 s y src_mac en el despacho (ciclo de sirena activo, AT-030) | `fix(nodered)` + `test:` |
| + | Rearme por estado: abierto -> sirena continua, cerrado -> silencio; el silencio manda (AT-031) | `fix(nodered)` + `test:` |
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
10. **"PANIC" no cabe en el event_code (4 bytes)** — hallazgo de la primera
    ronda de campo: el boton enviaba `PANI` y el toggle de Node-RED nunca
    corria (sin sonido, sin push; solo la fila `PANI` en `events`). El hub
    ahora normaliza `PANI` -> `PANIC` en el JSON (HUB-IF-002). Regla para
    codigos nuevos: maximo 4 caracteres en la trama.
11. **El hub no ACKea eventos de datos** (solo el PAIR del chequeo de
    enlace, HUB-IF-011): el reenvio ACK-gateado del boton triplicaba cada
    oprimida. Corregido a envio unico (patron REED) + dedup por
    (mac, evento, seq) en Node-RED para insercion y push. Leccion: antes de
    disenar reintentos, verificar que el receptor realmente confirme.
12. **Notificacion de prueba en el diagnostico** (peticion del usuario):
    cada consulta de salud envia un push de prueba por la cadena real de
    push y responde `prueba_notificacion: "enviada"`. Verificado: `PUSH OK
    201 (aviso)` y el push critico del PANIC recibido en el celular en la
    prueba de campo. Los pushes de la primera ronda salian `201 (media)`
    para los DOOR: la entrega push funcionaba; lo que faltaba era el PANIC
    clasificado critico (hallazgo 10).
13. **"Abro el reed y ya no suena" (campo 20:09-20:10)**: dos causas de
    flow, ninguna de radio. (a) El cooldown anti-retrigger de 60 s se comia
    las aperturas reales — bajo a 10 s porque el dedup por seq ya cubre los
    duplicados de transporte. (b) El despacho de alarma nunca llevaba
    `src_mac`, asi que el ciclo de sirena de CR-002 no se activaba para el
    reed (rafaga unica de `buzzer_seconds`). Con src_mac el hub re-verifica:
    rafagas mientras la puerta este abierta, fin al cerrar (verificado con
    captura MQTT: rafaga 1, rafaga 2 con el mismo tid, cierre -> fin).
    Los push de cada DOOR salieron `201 (media)` x2 tokens: la entrega en
    el servidor estaba sana; si el telefono no los muestra, revisar
    permisos de notificacion y que el navegador/PWA este vivo.
14. **Alarma por estado del reed (peticion del usuario, 20:4x)**: "abierto ->
    sona; cerrado -> silencio; no por cambio de estado". El ciclo del hub ya
    daba la continuidad una vez armado, pero el armado era por transicion y
    se perdia con reinicios y silencios. Solucion en el flow: rearme por
    estado cada 60 s (lee el ultimo DOOR/HEAR de events por reed) que
    re-dispatcha la sirena si esta abierta, **respetando el silencio
    vigente** (BUZZ value=0 lo fija; DOOR value=1 lo expira). No toco
    firmware: sin inundar events con reportes periodicos del nodo.
15. **Simular transiciones por MQTT desincroniza el estado del hub**:
    los eventos publicados a `event` solo llegan a Node-RED/BD — el hub
    aprende el estado del sensor por ESP-NOW. En una prueba, un "cierre"
    simulado no llego al hub y su ciclo siguio disparando rafagas ~3.5 min
    (el usuario las escucho y oprimio el panico para pararlas). Para probar
    el ciclo del hub, usar transiciones reales del sensor.

## Verificación (resumen; el detalle crudo está en SPEC-03)

| AT | Alcance | Resultado |
|---|---|---|
| AT-024 | Registro, pairing, HEAR auditable | PASS |
| AT-025 | 1.ª oprimida → toggle → `seconds=65535` → `BUZZ value=1` (bucle latched) | PASS (buzzer actualizado) |
| AT-026 | 2.ª oprimida → `seconds=0` → `BUZZ value=0` + dedup seq | PASS |
| AT-027 | Silenciar del portal con toggle armado | PASS |
| AT-028 | «Probar ESP-NOW» (rtt 65 ms) y salud del botón | PASS |

## Pendientes

- **Flashear el botón con el envío único** (`fix` commiteado): requiere un
  momento de USB; mientras tanto su firmware vigente reenvia 3 veces y el
  dedup de Node-RED lo colapsa (verificado en campo, AT-029).
- **Editar nombre/ubicación del botón en el portal**: los placeholders
  ("Sin Habitación") salen en el título del push ("🚨 ALARMA — Sin
  Habitación"); con el cuarto bien puesto el titulo leera la habitación.
- **Alcance**: si el botón se mueve a otro piso, validar el enlace con
  «Probar ESP-NOW» antes de fijarlo (ESP-NOW no atraviesa bien varias losas).

## Referencias

- Spec: `docs/firmware/specs/02-interfaces.md` (v0.6, HUB-IF-002/005/006/015).
- Pruebas: `docs/firmware/specs/03-pruebas-aceptacion.md` (AT-024..AT-028).
- Firmware: `firmware/boton-panico/` (nodo), `firmware/hub/main/main.c`
  (PANIC + latch), `firmware/buzzer/main/main.c` (0xFFFF).
- Lecciones previas: `docs/operaciones/2026-10-30-duplicado-nodos-y-ping.md`
  y `docs/operaciones/2026-10-30-integracion-buzzer.md`.
