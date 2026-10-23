# Base de Datos seguimiento360_alarmas

PostgreSQL 16 en contenedor postgres (usuario admin), red iot-alarma_default.

## Aplicar esquema
docker exec -i postgres psql -U admin -d seguimiento360_alarmas < backend/database/schema.sql

## Tablas
users, tenants, sites, devices, device_credentials, device_audit_log

## Conexion desde Node-RED
Host postgres (DNS interno de Docker), puerto 5432. Config en functionGlobalContext.dbConfig.

## Dump y restauracion
docker exec postgres pg_dump -U admin seguimiento360_alarmas > dump.sql
docker exec -i postgres psql -U admin -d seguimiento360_alarmas < dump.sql
