# CAPÍTULO 8: DESPLIEGUE, OPERACIÓN Y CONTINUIDAD

## 8.1 Infraestructura del Servidor

El sistema opera sobre Ubuntu Server 22.04 LTS con arquitectura de contenedores Docker Compose en la red iot-alarma_default. Los requisitos minimos son dos nucleos de CPU, 4 GB de RAM (recomendado 8 GB), 50 GB de SSD y conexion estable de 10 Mbps. Los servicios que componen la plataforma son: Mosquitto (broker MQTT), Traccar (geolocalizacion), Node-RED (logica de negocio y API), InfluxDB (series temporales), PostgreSQL (datos relacionales), Grafana (dashboards), Gotify (notificaciones), Nginx (archivos estaticos y PWA), Cloudflared (tunel) y Uptime-Kuma (monitoreo).

La exposicion a internet se realiza exclusivamente mediante Cloudflare Tunnel con terminacion TLS en el borde, sin puertos abiertos en el router; el firewall del host (UFW) permite unicamente los puertos 22, 80 y 443. Los dispositivos y usuarios acceden siempre por HTTPS/WSS sobre el puerto 443 mediante los subdominios del dominio seguimiento360.com.

## 8.2 Configuración de Servicios y Checklist de Puesta en Marcha

Todo despliegue nuevo o reinstalacion debe completar el siguiente checklist, que combina la lista original de infraestructura con el endurecimiento de seguridad definido en el Capitulo 7:

    [ ] Docker y Docker Compose instalados
    [ ] Dominio configurado en Cloudflare y tunnel activo
    [ ] Certificados SSL activos en el borde
    [ ] Firewall UFW configurado (22, 80, 443)
    [ ] Contraseñas por defecto cambiadas (Grafana, Gotify, InfluxDB, Traccar)
    [ ] Mosquitto con autenticacion y ACL (allow_anonymous false)
    [ ] Node-RED con adminAuth habilitado
    [ ] Backups automaticos configurados (servidor y desarrollo)
    [ ] Monitoreo activo en Uptime-Kuma con alertas al operador
    [ ] Documentacion actualizada

La gestion operativa diaria usa los comandos estandar de Docker Compose (ps, logs, restart, up, down) y los scripts de automatizacion start.sh, stop.sh y status.sh documentados en la infraestructura base.

## 8.3 Provisioning de Dispositivos

El alta de un dispositivo sigue el flujo definido en el Capitulo 6: registro de fabrica del numero de serie en el backend, vinculacion por el cliente mediante serie o QR con codigo de activacion de un solo uso, entrega de credenciales MQTT y configuracion por BLE o punto de acceso temporal, y persistencia en NVS. La desvinculacion libera el dispositivo (status disponible) conservando auditoria, y dispara un comando de reset de fabrica si el equipo esta en linea. Este flujo permite que el cliente final autogestione sus dispositivos sin intervencion tecnica, requisito del modelo de autoservicio.

## 8.4 Monitoreo y Alertas Operativas

El monitoreo opera en tres niveles. Nivel de servicios: Uptime-Kuma verifica disponibilidad de cada URL publica y notifica al operador ante caidas. Nivel de plataforma: docker stats y logs para recursos y errores, con alertas de disco y memoria. Nivel de dispositivos: watchdog de heartbeats configurable desde el panel (umbral por tipo de dispositivo), testamento MQTT para desconexiones abruptas, y alertas de bateria baja reportadas por los propios nodos. Las alertas operativas se canalizan por Gotify y Telegram al super-administrador, separadas de las alertas de seguridad de los clientes.

## 8.5 Copias de Seguridad del Servidor

El procedimiento de backup del servidor protege configuraciones y datos criticos: configuracion de Mosquitto (incluye credenciales y ACL), datos y configuracion de Traccar, flujos y configuracion de Node-RED, docker-compose.yml, respaldo portable de InfluxDB y datos de Grafana. El script consolidado genera un archivo comprimido fechado:

    #!/bin/bash
    BACKUP_DIR=~/backups/$(date +%Y%m%d_%H%M%S)
    mkdir -p $BACKUP_DIR
    cp -r ~/iot-alarma/mosquitto/config $BACKUP_DIR/
    cp -r ~/iot-alarma/traccar $BACKUP_DIR/
    cp -r ~/iot-alarma/nodered/data $BACKUP_DIR/
    cp ~/iot-alarma/docker-compose.yml $BACKUP_DIR/
    docker exec influxdb influxd backup -portable /var/lib/influxdb2/backup
    docker cp influxdb:/var/lib/influxdb2/backup $BACKUP_DIR/influxdb_backup
    docker cp grafana:/var/lib/grafana $BACKUP_DIR/grafana_data
    tar -czf ~/backups/backup_$(date +%Y%m%d_%H%M%S).tar.gz $BACKUP_DIR
    rm -rf $BACKUP_DIR

Se programa ejecucion diaria mediante cron y se mantiene rotacion que conserve al menos los ultimos siete respaldos diarios y cuatro semanales, almacenados en el disco externo montado en /mnt/disco-externo con entrada fstab de arranque seguro (opcion nofail).

## 8.6 Estrategia de Respaldo del Desarrollo (3-2-1)

El codigo, la documentacion (incluido este marco teorico) y los diseños de hardware se protegen con la regla 3-2-1: tres copias, en dos medios distintos, y una fuera del sitio. La copia original reside en el equipo de desarrollo; la copia fuera del sitio es el repositorio privado en GitHub con push al cierre de cada sesion; y la copia fisica local es un disco externo NTFS con respaldo automatico semanal.

El script de respaldo del desarrollo ejecuta: commit de seguridad si existen cambios sin versionar, creacion de un bundle Git con el historial completo, sincronizacion rsync de los archivos de trabajo, y respaldo de las configuraciones del servidor iot-alarma cuando coexisten en la misma maquina. Mantiene rotacion de los ultimos siete respaldos. La programacion es semanal (domingos 23:00) via cron con registro en archivo de log.

La restaurabilidad se verifica mediante pruebas periodicas: un bundle debe poder clonarse en un directorio temporal y reproducir el historial completo con git log. Un backup que no ha sido probado no se considera valido.

## 8.7 Recuperación ante Desastres

Se definen los escenarios de falla con sus objetivos de punto de recuperacion (RPO) y tiempo de recuperacion (RTO), y sus procedimientos:

| Escenario | RPO | RTO | Procedimiento |
|---|---|---|---|
| Caida de un servicio | 0 | Menos de 5 min | Reinicio por restart policy o docker compose restart |
| Corrupcion de datos de plataforma | 24 h | Menos de 1 h | Restaurar configuraciones y backup de InfluxDB |
| Perdida total del servidor | 24 h | Menos de 4 h | Nuevo host, Docker, restaurar backup, reconfigurar tunnel |
| Perdida del equipo de desarrollo | 0 | Menos de 1 h | git clone desde GitHub |
| Indisponibilidad de GitHub | Ultimo push | Menos de 1 h | git clone desde bundle del disco externo |
| Credencial comprometida | N/A | Inmediato | Revocacion y reemision desde el panel; expulsión del broker |

Procedimiento de restauracion del servidor:

    docker compose down
    tar -xzf backup_YYYYMMDD_HHMMSS.tar.gz
    cp -r backup_*/mosquitto/config ~/iot-alarma/
    cp -r backup_*/traccar ~/iot-alarma/
    cp -r backup_*/nodered/data ~/iot-alarma/
    docker compose up -d
    (restaurar InfluxDB con influxd restore -portable tras copiar el respaldo al contenedor)

Tras toda restauracion se ejecuta el checklist de puesta en marcha (8.2) y se verifican los monitores de Uptime-Kuma. Semestralmente se realiza un simulacro de recuperacion completa en hardware de prueba para validar tiempos reales y actualizar los procedimientos.

## 8.8 Resumen del Capítulo

El capitulo define la operacion completa de la plataforma: infraestructura de contenedores expuesta sin puertos abiertos, checklist de puesta en marcha con endurecimiento de seguridad, provisioning de autoservicio, monitoreo en tres niveles, y una estrategia de continuidad que combina backups diarios del servidor con la regla 3-2-1 para el desarrollo, rotacion de respaldos, pruebas de restauracion y escenarios de recuperacion con objetivos cuantificados de RPO y RTO.
