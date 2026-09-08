# 📡 Seguimiento 360 - Plataforma IoT de Rastreo y Telemetría

![Version](https://img.shields.io/badge/Version-v0.3.0--phase3-blue)
![Status](https://img.shields.io/badge/Status-Production_Ready-green)
![Hardware](https://img.shields.io/badge/Hardware-ESP32_S3_%7C_ESP32-orange)
![Protocol](https://img.shields.io/badge/Protocol-ESP--NOW_M2bis-purple)

Plataforma integral de rastreo GPS y telemetría de alarmas en tiempo real. Utiliza una arquitectura híbrida de borde (Edge) con **ESP-NOW (M2bis)** para comunicaciones de ultra baja latencia, y **MQTT over WSS** seguro vía Cloudflare Tunnel para la ingestión de datos en la nube.

---

##  Tabla de Contenidos
1. [Arquitectura del Sistema](#1-arquitectura-del-sistema)
2. [Infraestructura de Servidor](#2-infraestructura-de-servidor)
3. [Firmware y Protocolo M2bis](#3-firmware-y-protocolo-m2bis)
4. [Esquema de Datos y Tópicos](#4-esquema-de-datos-y-tópicos)
5. [URLs y Puntos de Acceso](#5-urls-y-puntos-de-acceso)
6. [Guía de Despliegue y Mantenimiento](#6-guía-de-despliegue-y-mantenimiento)
7. [Seguridad y Recomendaciones](#7-seguridad-y-recomendaciones)

---

## 1. Arquitectura del Sistema

El sistema se divide en tres capas principales, diseñadas para operar sin puertos abiertos en el firewall del servidor:

###  Capa de Borde (Edge Devices)
*   **ESP32 Node (Sensor/Alarma):** Dispositivo de bajo consumo que captura eventos (GPS, alarmas). Se comunica con el Hub mediante **ESP-NOW** (Protocolo M2bis).
*   **ESP32 Hub (Gateway):** Actúa como Station (STA) hacia el router local y como Access Point (AP) para los Nodos. Recopila datos y los envía a la nube.

###  Capa de Transporte
*   **MQTT over WSS:** El Hub publica datos en el broker Mosquitto utilizando WebSockets Seguros (WSS) a través del **Puerto 443**.
*   **Cloudflare Tunnel:** Encriptación SSL/TLS automática, protección DDoS y routing inteligente.

### ☁️ Capa de Servidor (Backend)
*   **Ingesta y Lógica:** Node-RED procesa los mensajes MQTT y los transforma a Line Protocol.
*   **Almacenamiento:** InfluxDB (Time-Series Database) almacena la telemetría.
*   **Visualización y Rastreo:** Grafana (Dashboards) y Traccar (Geolocalización GPS).
*   **Notificaciones:** Gotify + Telegram Bot para alertas en tiempo real.

---

## 2. Infraestructura de Servidor

El backend está 100% containerizado utilizando **Docker Compose** en una red aislada (`iot-alarma_default`).

| Servicio | Imagen | Puerto Interno | Función Principal |
| :--- | :--- | :--- | :--- |
| **Mosquitto** | `eclipse-mosquitto` | 1883 / 8083 | Broker MQTT (TCP y WebSockets) |
| **Traccar** | `traccar/traccar` | 8082 / 5000-5160 | Plataforma de rastreo GPS (OsmAnd) |
| **Node-RED** | `nodered/node-red` | 1880 | Automatización y parsing de datos |
| **InfluxDB** | `influxdb:2.7` | 8086 | Base de datos de series de tiempo |
| **Grafana** | `grafana/grafana` | 3000 | Visualización de métricas |
| **Gotify** | `gotify/server` | 80 | Servidor de notificaciones push |
| **Nginx** | `nginx:alpine` | 80 | Servidor web para archivos estáticos |
| **Cloudflared** | `cloudflare/cloudflared` | - | Túnel seguro hacia Cloudflare |
| **Uptime-Kuma** | `louislam/uptime-kuma` | 3001 | Monitoreo de estado de servicios |

---

## 3. Firmware y Protocolo M2bis

El protocolo propietario **M2bis** (Mesh 2 Bis) gestiona la comunicación inalámbrica entre el Node y el Hub.

*   **Topología:** APSTA (Access Point + Station). El Hub crea una red Wi-Fi exclusiva para los Nodos.
*   **Protocolo RF:** ESP-NOW (Capa 2, sin handshake TCP/IP, latencia ultra baja).
*   **Mecanismo de Fiabilidad:** ACK Inline (El Hub responde en el mismo callback de RF) + CRC16.
*   **Payload:** Estructura binaria compacta de 29 bytes.
*   **Rendimiento Validado (FASE 3):**
    *   Entrega de Paquetes: **100% (50/50 frames)**
    *   RTT Promedio: **17.11 ms** (Criterio AT-02: <50ms ✅)

---

## 4. Esquema de Datos y Tópicos

### 📡 Tópicos MQTT
*   **Publicación (Hub → Broker):** 
    `seguimiento360/tenant/{tenant_id}/site/{site_id}/hub/{hub_mac}/event`
*   **Suscripción (Node-RED):** 
    `seguimiento360/tenant/+/site/+/hub/+/event`

### 🗄️ Esquema InfluxDB
*   **Organización:** `seguimiento360`
*   **Bucket:** `telemetria` (Retención: 90 días)
*   **Measurements:**
    *   `gps_data`: `latitude`, `longitude`, `altitude`, `speed`, `course`, `satellites`, `hdop`.
    *   `alarm_event`: `event_code`, `message`, `seq`, `system_state`.
*   **Tags (Indexados):** `device_id` (MAC), `device_type`.

---

## 5. URLs y Puntos de Acceso

Todos los servicios externos están protegidos por SSL/TLS vía Cloudflare.

###  URLs Públicas (HTTPS)
| Servicio | URL | Credenciales (Default) |
| :--- | :--- | :--- |
| **Web Principal** | `https://seguimiento360.com` | Pública |
| **Panel Rastreo (Traccar)**| `https://app.seguimiento360.com` | admin / admin |
| **Broker MQTT (WSS)** | `wss://mqtt.seguimiento360.com` | Puerto 443 (Sin auth) |
| **Automatización (Node-RED)**| `https://nodered.seguimiento360.com` | Sin auth |
| **Dashboards (Grafana)** | `https://grafana.seguimiento360.com` | admin / admin |
| **Notificaciones (Gotify)**| `https://gotify.seguimiento360.com` | admin / admin |
| **Monitoreo (Uptime-Kuma)**| `https://uptime.seguimiento360.com` | Sin auth |

### 🔒 URLs Internas (Red Docker `iot-alarma_default`)
*   Mosquitto MQTT: `mqtt://mosquitto:1883`
*   Mosquitto WS: `ws://mosquitto:8083`
*   InfluxDB: `http://influxdb:8086`
*   Node-RED: `http://nodered:1880`

---

## 6. Guía de Despliegue y Mantenimiento

### 🖥️ Servidor (Backend)
```bash
# Clonar y levantar servicios
cd ~/iot-alarma
docker compose up -d

# Verificar estado y logs
docker compose ps
docker compose logs -f nodered# seguimiento360-alarma


## 8. FASE 4: Sistema de Seguridad y Notificaciones (Gotify)

Integración exitosa del ciclo de alertas para el sistema de seguridad doméstica.

### Flujo de Notificación
`Sensor (NODO) -> ESP-NOW -> HUB -> MQTT -> Node-RED -> Gotify -> Celular`

### Códigos de Alarma Soportados
El filtro de seguridad en Node-RED dispara notificaciones push para los siguientes eventos:
* `ALRM`: Alarma general
* `MOV`: Detección de movimiento
* `GAS`: Fuga de gas
* `DOOR`: Apertura de puerta/ventana
* `SEIS`: Detección sísmica
* `SOS` / `PANIC`: Botón de pánico
* `HUM`: Detección de presencia humana

### Validación de Extremo a Extremo
- [x] HUB y NODO comunicándose vía ESP-NOW (RTT ~17ms).
- [x] Datos persistidos en InfluxDB (Bucket: telemetria).
- [x] Notificaciones push recibidas en dispositivo móvil vía app nativa de Gotify.
