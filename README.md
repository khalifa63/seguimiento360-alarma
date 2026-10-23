# Seguimiento 360 · Alarma

Plataforma IoT de alarma y monitoreo para hogar y vehículos: nodos ESP32 → hub ESP32-S3 por ESP-NOW (protocolo M2bis) → MQTT over WSS → Node-RED → PostgreSQL/InfluxDB → PWA y notificaciones.

**Documentación completa: [docs/README.md](docs/README.md)**

## Estado

- Autenticación JWT multi-tenant, sitios, dispositivos y códigos de vinculación.
- Armado/desarmado por sitio desde la PWA.
- Portal cautivo y OTA en el hub.
- Telemetría y alertas por MQTT (InfluxDB + Gotify).
- Enlace ESP-NOW validado: ACK < 50 ms.

Detalle y pendientes: [Estado por fases](docs/fases/RESUMEN_FASES.md).

## Arquitectura en 30 segundos

```text
Nodos ESP32 ──ESP-NOW──> Hub ESP32-S3 ──MQTT/WSS:443──> Cloudflare Tunnel
                                                              │
                                                              ▼
                                          Node-RED ──> PostgreSQL / InfluxDB
                                              │              │
                                              └──> PWA <─────┘
                                                   └──> Gotify / Telegram
```

## Arranque rápido

```bash
git clone https://github.com/khalifa63/seguimiento360-alarma.git
cd seguimiento360-alarma
cp .env.example .env          # completa las credenciales
cp nodered/data/settings.js.example nodered/data/settings.js
```

El despliegue completo del servidor está en [Manual general](docs/01-manual-general.md) y [Estado actual](docs/infraestructura/ESTADO_ACTUAL.md).

## Estructura

```text
backend/        # flujos Node-RED, esquema SQL
firmware/       # hub, nodos y simulador ESP32
frontend/       # PWA
docs/           # única fuente de documentación
nodered/        # plantilla de settings.js
infrastructure/ # reservado para Docker, Nginx, Mosquitto, CI/CD
hardware/       # reservado para BOM, PCB, esquemáticos
scripts/        # operación y despliegue
tests/          # pruebas de integración
```

## Seguridad

- No se versionan `.env`, `settings.js` reales, `build/` ni `sdkconfig`.
- Reporta vulnerabilidades según [SECURITY.md](SECURITY.md); no abras un issue público con credenciales.

## Licencia

Proyecto privado. © 2026 Antonio. Todos los derechos reservados.
