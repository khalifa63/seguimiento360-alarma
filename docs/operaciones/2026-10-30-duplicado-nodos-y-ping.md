# 2026-10-30: nodos duplicados en el portal + prueba de enlace que fallaba

Dos defectos del prototipo REED detectados al probar el portal con el nodo
vinculado. Ambos corregidos y verificados.

---

## 1. El nodo aparecia duplicado en el portal

**Sintoma:** la lista de nodos mostraba "Nodo REED" dos veces, con la misma
MAC. Los botones de salud y prueba de en ambos fallaban.

**Causa:** la tabla `devices` tenia DOS filas con la misma MAC en distinta
capitalizacion:

    id 5 | 00:70:07:1c:6f:a4  | sin firmware_version | activo 00:01
    id 7 | 00:70:07:1C:6F:A4  | reed-v0.3            | bound 13:56

El `UNIQUE (serial_number)` de la tabla es sensible a mayusculas: la
capitalizacion distinta elude la restriccion y permite duplicados.

La fila 5 la inserto un script de alta manual (`/tmp/pair_reed.sh`) con la
MAC en MAYUSCULAS; la 7 (correcta) la inserto la vinculacion real. El
`LEFT JOIN devices ... ON lower(d.serial_number) = lower(n.mac_address)` de
las consultas del portal (`SELECT overview`, `SELECT salud del nodo`) casa
con las dos filas y multiplica cada nodo.

**Correccion:**

1. Backup de la tabla (`/tmp/devices_backup_20261030.csv`).
2. Borrada la fila vieja (id 5), normalizada la buena a minusculas.
3. Indice unico insensible al caso: `devices_serial_lower_idx` sobre
   `lower(serial_number)` — la barrera anti-duplicado definitiva.
4. `docs/infraestructura/FLUJOS_NODERED.md`: el alta de devices documenta
   insertar con `lower($4)`.

**Verificacion:** un INSERT con la MAC en mayusculas es rechazado por el
indice; el overview devuelve una fila por nodo.

---

## 2. El boton «Probar ESP-NOW» daba error con el enlace vivo

**Sintoma:** el portal reportaba "sin_respuesta" en la prueba de enlace,
mientras el DOOR y las notificaciones funcionaban perfectamente.

**Causa:** el hub envia el ping como frame `ESPNOW_FT_CMD` con
`espnow_payload_ping_t` (magia `0xE7`). El firmware del nodo reed solo
procesaba frames `ESPNOW_FT_ACK` en `on_data_recv()` — **descartaba todo lo
demas**, incluido el ping. El hub agotaba 3 intentos y publicaba
`PING_RESULT ok:false reason:"sin_respuesta"`.

La comunicacion real funcionaba porque va en la direccion contraria
(nodo→hub, DATA_EVENT), mientras el ping es hub→nodo→respuesta: ese tramo no
existia.

**Correccion** (`firmware/reed-sensor/main/main.c`, commit 2d79d0b): el nodo
atiende `ESPNOW_FT_CMD` con magia 0xE7 y responde con
`send_frame("PONG", frame.seq)`. El hub ya sabia interpretar el PONG
(evento "PONG" en su recv, resuelto localmente sin subir a la BD).

**Verificacion:** `PING_RESULT ok:true rtt_ms:69 attempts:1`; pings
simultaneos desde el portal: ok:true con rtt 61-68 ms. El PONG no aparece en
la tabla `events`.

---

## Leccion

Los dos fallaron por validaciones incompletas, no por code paths exoticos:
- El alta manual de devices no normalizaba el caso del serial, y la restriccion
  UNIQUE no lo cubria. La regla es: **serial siempre minusculas** + indice
  sobre `lower()`.
- El nodo asumia que todo frame que no fuera ACK no era suyo. Al agregar una
  direccion de trafico nueva (CMD del hub), el recv del nodo se quedo corto.

## Referencias

- Spec del PING/PONG y del barrido de canales: `docs/firmware/specs/02-interfaces.md`
  (HUB-IF-006 y HUB-IF-011).
- Commits: `9c0c593` (doc anti-duplicados), `2d79d0b` (PONG del reed).
