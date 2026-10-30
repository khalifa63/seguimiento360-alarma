# Integración del nodo buzzer — 2026-10-30

La integración del módulo buzzer como nodo de alarma quedó completa de extremo
a extremo: spec (SPEC-02 v0.4), migración de BD, firmware del hub y del nodo
(ESP32-C3), despacho en Node-RED, portal editable y verificación con pruebas
de aceptación (AT-020..AT-023). Este documento deja rastro de decisiones,
hallazgos y lo que quedó pendiente.

## Qué se hizo

| Fase | Contenido | Commit |
|---|---|---|
| 1 | SPEC-02 v0.4: `BUZZ`, comando `buzzer`, `espnow_payload_buzzer_t` (HUB-IF-015) | `spec:` x2 |
| 2 | Columna `nodes.buzzer_seconds smallint` + snapshot pg_dump | `feat(db)` |
| 3 | Hub: handler del comando + envío ESP-NOW + evento BUZZ | `feat(hub)` |
| 4 | `firmware/buzzer/`: nodo sirena con pairing, PONG, HEAR, LEDC | `feat(buzzer)` |
| 5 | Node-RED: clasificación de BUZZ, despacho con cooldown, snapshot saneado | `feat(nodered)` |
| 6 | Portal: `buzzer_seconds` editable + overview SQL | `feat(portal)` |
| 7 | `POST /api/nodes` registra `device_type` + `buzzer_seconds` | `feat(api)` |
| 8 | AT-020..AT-023 con evidencia cruda | `test:` |

## Decisiones

- **La decisión de disparar vive en Node-RED** (config en la BD), no en el
  firmware del hub. El hub solo lleva el comando por ESP-NOW.
- **El BUZZ sube a la BD** a diferencia del PONG: la activación de la sirena
  es dato operativo auditable (`events`, clasificado `media`).
- **Extensión, no reinicio:** si el buzzer recibe otro comando mientras suena,
  extiende el deadline en lugar de reiniciarlo; un trigger repetido no corta
  una alarma más larga.
- **El `src_event` es opcional** en el JSON del comando; si no viene, el hub
  rellena `ALRM`.

## Hallazgos

- **Brownout del hub (bloqueo de la verificación).** Durante la sesión el hub
  entró en loop de reinicio por *brownout* al activar el radio (`I BOD:
  Brownout detector was triggered` en `phy_init`, ~1.2 s tras el boot),
  con `rst:0xc (RTC_SW_CPU_RST)`. Misma lección del REED: la alimentación por
  USB débil no alcanza; reenchufado a fuente estable, arrancó limpio y quedó
  online. El firmware en el dispositivo en ese momento era v6.1.0 (Oct 24);
  tras reenchufar se flasheó la build nueva con el handler del buzzer.
- **Log de corte de sirena engañoso.** La primera versión del firmware del
  buzzer imprimía `Sirena detenida tras 105 s` — el epoch del deadline, no la
  duración real (~10 s). Corregido: se guarda el instante de arranque
  (`s_buzz_started_s`) y el corte reporta la duración efectiva.
- **`tid` truncado a 12 caracteres** en el payload ESP-NOW (`tid[13]`); los
  tids largos de Node-RED llegan al diag recortados (`mvvfakfh-csc`). Opaco,
  mismo límite que el ping; no rompe el trazado.
- **`node_name` vacío en el BUZZ inicial.** El registro interno del hub se
  crea en el pairing con placeholders vacíos; se corrigió publicando un
  `node_cfg` con nombre/ubicación/habitación.
- **El PAIR cada 10 s es el chequeo de enlace normal** (igual que el REED):
  no confundir con un nodo desvinculado; el indicador real es
  `Hub vinculado en NVS` en el arranque y la llegada de `HEAR`.

## Pendientes

- **CR-002 — disparo local en el hub** para operar sin internet
  (Node-RED caído): decidir en el hub y mantener el cooldown local.
- **Push de BUZZ a Firebase/Gotify**: el flujo clasifica BUZZ como media y
  genera el push, pero la entrega depende de tokens FCM registrados; verificar
  con un dispositivo real (fuera de alcance de esta sesión).
- **Cooldown solo en Node-RED**: si Node-RED se reinicia, el contexto
  (`buzzer_ult`) se pierde y un DOOR pegado puede re-disparar una vez.
  Candidato a mover a la BD si molesta.
- **Cambios ajenos sin commitear en el árbol** (`sensor-ultrasonic/`,
  `backend/nodered-flows/README.md`): proyectos independientes, no se tocaron.
