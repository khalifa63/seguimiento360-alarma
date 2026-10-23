# Resumen de Rutas y Puertos - Cloudflare y Servidor

## Exposicion publica
Solo 22 (SSH), 80 y 443 permiten trafico entrante (UFW). Todo el acceso web entra por Cloudflare (443) via tunnel, sin puertos abiertos hacia los servicios.

## Rutas del tunnel (Cloudflare Zero Trust, Public Hostnames)

| Subdominio | Origen | Uso | Autenticacion |
|---|---|---|---|
| seguimiento360.com | http://nginx:80 | Web principal y PWA | Publica |
| app.seguimiento360.com | http://traccar:8082 | Panel GPS | Usuario y clave |
| mqtt.seguimiento360.com | http://mosquitto:8083 | MQTT-WSS para dispositivos | Pendiente (anonimo) |
| nodered.seguimiento360.com | http://nodered:1880 | Automatizacion y API | Pendiente adminAuth |
| grafana.seguimiento360.com | http://grafana:3000 | Dashboards | Usuario y clave |
| gotify.seguimiento360.com | http://gotify:80 | Notificaciones | Usuario y clave |
| uptime.seguimiento360.com | http://uptime-kuma:3001 | Monitoreo | Pendiente |

Nota: el 2026-08-06 se observo el origen de nodered apuntando a `http://<IP-DEL-HOST>:1880`. Funciona porque el puerto esta publicado, pero la forma robusta es el nombre de contenedor. Se recomienda estandarizar todos los origenes a nombres de contenedor.

## Puertos publicados por Docker (visibles en la LAN)

| Servicio | Puertos | Observacion |
|---|---|---|
| mosquitto | 1883, 8883, 8083, 9001 | 8883 sin TLS configurado; 9001 sin uso |
| traccar | 8082, 5000-5160 | 5055 protocolo OsmAnd |
| nodered | 1880 | Editor y API |
| influxdb | 8086 | Deberia ser solo interno |
| grafana | 3000 | |
| gotify | 8081 | |
| nginx | 80 | |
| uptime-kuma | 3001 | |
| postgres | 5432 | Verificar con docker port postgres |

## Recomendaciones de endurecimiento (por fases, sin romper lo operativo)

1. Cambiar los origenes del tunnel a nombres de contenedor (nodered:1880, traccar:8082, etc.)
2. Una vez hecho lo anterior, restringir los puertos publicados a 127.0.0.1 (ejemplo: 127.0.0.1:1880:1880) sin afectar el tunnel
3. Retirar 8883 y 9001 de mosquitto hasta que se necesiten
4. Verificar la exposicion de postgres y restringirla si esta publicada
5. Autenticacion MQTT (passwd + ACL) antes del lanzamiento comercial de alarmas
