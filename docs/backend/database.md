# Base de Datos seguimiento360_alarmas

PostgreSQL 16 en contenedor postgres (usuario admin), red iot-alarma_default.

## Aplicar esquema
docker exec -i postgres psql -U admin -d seguimiento360_alarmas < backend/database/schema.sql

## Tablas
users, tenants, sites, devices, device_credentials, device_audit_log,
hubs, nodes, events, alarm_state, claim_codes, user_fcm_tokens

## Conexion desde Node-RED
Host postgres (DNS interno de Docker), puerto 5432. Config en functionGlobalContext.dbConfig.

## Dump y restauracion
docker exec postgres pg_dump -U admin seguimiento360_alarmas > dump.sql
docker exec -i postgres psql -U admin -d seguimiento360_alarmas < dump.sql

## Esquema real en produccion (2026-10-30)

`backend/database/schema.sql` contiene el volcado REAL de la base
`seguimiento360_alarmas` (pg_dump --schema-only): 12 tablas — users, tenants,
sites, devices, device_credentials, device_audit_log, hubs, nodes, events,
alarm_state, claim_codes y user_fcm_tokens.

Regenerarlo:

    PGPASS=$(docker exec nodered printenv PG_PASSWORD)
    docker exec -e PGPASSWORD="$PGPASS" postgres pg_dump -U admin \
      -d seguimiento360_alarmas --schema-only --no-owner --no-privileges \
      > backend/database/schema.sql

Tablas centrales del flujo de eventos:

- `events`: una fila por evento (event_type, battery_level, sequence_number,
  payload jsonb). La columna de medicion es `value` (SPEC-02 v0.3; antes
  `distance_cm`, renombrada el 2026-10-29 conservando el historico).
- `nodes`: registro runtime del nodo (mac_address, name, location, room,
  status, last_seen); la ingesta de eventos la actualiza en cada contacto.
  Desde el 2026-10-30 tiene `buzzer_seconds smallint` (HUB-IF-015): duracion
  de sirena por evento en segundos; 0 = desactivado, NULL = usa el default
  30. Solo se usa para `device_type='actuador_buzzer'`; el portal la edita
  con el mismo formulario que `distance_threshold`.
