# Seguimiento 360 · Plataforma IoT de Rastreo y Telemetría

> Manual general v1.2.0 (octubre 2026). Para el estado actual, consulta [Estado por fases](fases/RESUMEN_FASES.md).

![Version](https://img.shields.io/badge/Version-v1.2.0-blue)
![Status](https://img.shields.io/badge/Status-Production_Ready-green)
![Hardware](https://img.shields.io/badge/Hardware-ESP32_S3_%7C_ESP32-orange)
![Protocol](https://img.shields.io/badge/Protocol-ESP--NOW_M2bis-purple)

Plataforma integral de rastreo GPS y telemetría de alarmas en tiempo real. Utiliza una arquitectura híbrida de borde (Edge) con **ESP-NOW (Protocolo M2bis)** para comunicaciones de ultra baja latencia entre dispositivos, y **MQTT over WSS** para la conexión segura con la nube. Incluye portal web multi-tenant con autenticación JWT para gestión de dispositivos y visualización de datos.

---

## 📋 Tabla de Contenidos
1. [Arquitectura del Sistema](#1-arquitectura-del-sistema)
2. [Infraestructura de Servidor](#2-infraestructura-de-servidor)
3. [Firmware y Protocolo M2bis](#3-firmware-y-protocolo-m2bis)
4. [Portal Web y Autenticación](#4-portal-web-y-autenticación)
5. [Portal Cautivo (Provisioning)](#5-portal-cautivo-provisioning)
6. [Esquema de Datos y Tópicos](#6-esquema-de-datos-y-tópicos)
7. [Base de Datos (PostgreSQL + InfluxDB)](#7-base-de-datos-postgresql--influxdb)
8. [API REST - Endpoints](#8-api-rest---endpoints)
9. [URLs y Puntos de Acceso](#9-urls-y-puntos-de-acceso)
10. [Guía de Despliegue y Mantenimiento](#10-guía-de-despliegue-y-mantenimiento)
11. [Seguridad y Recomendaciones](#11-seguridad-y-recomendaciones)

---

## 1. Arquitectura del Sistema

El sistema se divide en tres capas principales, diseñadas para operar sin puertos abiertos en el firewall del servidor:

###  Capa de Borde (Edge Devices)
*   **ESP32 Node (Sensor/Alarma):** Dispositivo de bajo consumo que captura eventos (GPS, alarmas, sensores ultrasónicos). Se comunica con el Hub mediante **ESP-NOW** (Protocolo M2bis) en modo AP.
*   **ESP32 Hub (Gateway):** Actúa como Station (STA) hacia el router local y como Access Point (AP) para los Nodos. Recopila datos vía ESP-NOW y los envía a la nube vía MQTT over WSS.
*   **Portal Cautivo:** Interfaz web embebida en el Hub para configuración inicial de Wi-Fi y parámetros MQTT.

### 🌐 Capa de Transporte
*   **MQTT over WSS:** El Hub publica datos en el broker Mosquitto utilizando WebSockets Seguros (WSS) a través del **Puerto 443**.
*   **Cloudflare Tunnel:** Encriptación SSL/TLS automática, protección DDoS y routing inteligente. Cero puertos abiertos.

### ️ Capa de Servidor (Backend)
*   **Ingesta y Lógica:** Node-RED procesa los mensajes MQTT, valida autenticación JWT y transforma datos.
*   **Almacenamiento Dual:**
    *   **PostgreSQL:** Usuarios, tenants, hubs, nodos, configuración (datos relacionales).
    *   **InfluxDB:** Telemetría time-series (GPS, batería, sensores).
*   **Visualización y Rastreo:** Grafana (Dashboards), Traccar (Geolocalización GPS).
*   **Notificaciones:** Gotify + Telegram Bot para alertas en tiempo real.
*   **Portal Web:** SPA (Single Page Application) con autenticación JWT y aislamiento multi-tenant.

---

## 2. Infraestructura de Servidor

El backend está 100% containerizado utilizando **Docker Compose** en una red aislada (`iot-alarma_default`).

| Servicio | Imagen | Puerto Interno | Función Principal |
| :--- | :--- | :--- | :--- |
| **Mosquitto** | `eclipse-mosquitto` | 1883 / 8083 | Broker MQTT (TCP y WebSockets) |
| **Node-RED** | `nodered/node-red` | 1880 | Lógica de negocio, API REST, automatización |
| **PostgreSQL** | `postgres:15` | 5432 | Base de datos relacional (usuarios, dispositivos) |
| **InfluxDB** | `influxdb:2.7` | 8086 | Base de datos series de tiempo (telemetría) |
| **Grafana** | `grafana/grafana` | 3000 | Visualización de dashboards |
| **Traccar** | `traccar/traccar` | 8082 / 5000-5160 | Plataforma de rastreo GPS |
| **Nginx** | `nginx:alpine` | 80 | Reverse proxy y servidor web estático |
| **Gotify** | `gotify/server` | 80 | Servidor de notificaciones push |
| **Uptime-Kuma** | `louislam/uptime-kuma` | 3001 | Monitoreo de servicios |
| **Cloudflared** | `cloudflare/cloudflared` | - | Túnel Zero Trust hacia Cloudflare |

### 🗂️ Estructura de Directorios
```bash
~/seguimiento360-alarma/
├── docker-compose.yml
├── .env                          # Variables de entorno (NO subir a GitHub)
├── README.md
├── .gitignore
├── .env.example
├── firmware/
│   ├── hub/                      # Firmware ESP32 Hub (Gateway)
│   │   ├── main/
│   │   │   ├── main.c            # Punto de entrada
│   │   │   ├── ota_manager.c     # Actualizaciones OTA
│   │   │   └── wifi_manager.c    # Portal cautivo y Wi-Fi
│   │   ── CMakeLists.txt
│   ├── node_base/                # Firmware ESP32 Node (Sensor base)
│   └── sensor-ultrasonic/        # Firmware sensor ultrasónico
├── nodered/
│   ── data/
│       ├── settings.js           # Configuración (NO subir)
│       ├── settings.js.example   # Plantilla (SÍ subir)
│       └── flows.json            # Flujos de Node-RED
├── website/
│   ── portal/
│       ├── index.html            # Login
│       ├── registro.html         # Registro
│       └── dashboard.html        # Dashboard protegido
└── nginx/
    └── default.conf              # Configuración reverse proxy
```

---

## 3. Firmware y Protocolo M2bis

###  Protocolo ESP-NOW M2bis
El protocolo M2bis (Machine-to-Machine bis) es una implementación personalizada sobre ESP-NOW para comunicaciones de ultra baja latencia (<10ms) entre Hubs y Nodos.

**Características:**
*   **Sin handshake:** Comunicación one-to-many sin establecimiento de conexión.
*   **Payload optimizado:** Estructura binaria compacta (≤250 bytes).
*   **Encriptación AES-128:** Clave pre-compartida configurada en el portal cautivo.
*   **Retry automático:** Reintentos en caso de colisión de paquetes.

**Estructura del Payload M2bis:**
```c
typedef struct {
    uint8_t header;        // 0xAA (sync)
    uint8_t device_type;   // 0x01=Hub, 0x02=Node, 0x03=Sensor
    uint8_t msg_type;      // 0x01=Telemetry, 0x02=Alarm, 0x03=Heartbeat
    uint8_t payload_len;   // Longitud del payload (0-247)
    uint8_t payload[247];  // Datos JSON o binarios
    uint8_t checksum;      // XOR de todos los bytes anteriores
} m2bis_packet_t;
```

### 📡 Flujo de Comunicación
1.  **Node → Hub (ESP-NOW):** El sensor publica datos cada 5 segundos o ante evento de alarma.
2.  **Hub → Cloud (MQTT over WSS):** El Hub agrega los datos de múltiples Nodos y publica en `s360/telemetry/{hub_id}`.
3.  **Cloud → Hub (MQTT):** Comandos de configuración o actualizaciones OTA se publican en `s360/command/{hub_id}`.

###  Portal Cautivo (Provisioning)
Cuando el Hub se enciende por primera vez (sin credenciales Wi-Fi guardadas):
1.  Crea una red Wi-Fi AP: `S360_Hub_XXXX` (donde XXXX son los últimos 4 bytes de la MAC).
2.  Levanta un servidor web embebido en el puerto 80.
3.  El usuario se conecta desde su smartphone y accede a `http://192.168.4.1`.
4.  Ingresa: SSID Wi-Fi, contraseña, Broker MQTT (`mqtt.seguimiento360.com`), Puerto (443), y Token de autenticación.
5.  El Hub guarda en NVS (Non-Volatile Storage), se reinicia y conecta a la nube.

---

## 4. Portal Web y Autenticación

### 🔐 Sistema Multi-Tenant con JWT
El portal web (`seguimiento360.com/portal`) permite a los usuarios gestionar sus dispositivos de forma aislada.

**Flujo de Autenticación:**
1.  **Registro:** `POST /api/auth/register` → Crea usuario + tenant automáticamente.
2.  **Login:** `POST /api/auth/login` → Valida credenciales y devuelve JWT (24h de expiración).
3.  **Acceso Protegido:** Cada petición incluye `Authorization: Bearer <token>`.
4.  **Aislamiento:** Todas las consultas a BD filtran por `tenant_id` extraído del token.

**Endpoints Principales:**
*   `GET /api/auth/nodes` → Lista todos los nodos del tenant del usuario.
*   `GET /api/auth/profile` → Obtiene perfil del usuario.
*   `POST /api/auth/logout` → Invalida token (frontend limpia localStorage).

### ️ Frontend (SPA)
*   **Tecnología:** HTML5 + JavaScript vanilla (sin frameworks).
*   **Almacenamiento:** `localStorage` para JWT y datos de usuario.
*   **Protección:** Redirección automática a login si no hay token válido.
*   **Comunicación:** Fetch API para REST, Paho MQTT JS para WebSockets (tiempo real).

---

## 5. Portal Cautivo (Provisioning)

El firmware del Hub ESP32 incluye un **Portal Cautivo** para configuración inicial sin necesidad de aplicación móvil.

### Configuración del Portal Cautivo
**Red Wi-Fi del Hub:**
*   **SSID:** `S360_Hub_XXXX` (XXXX = últimos 4 bytes de MAC)
*   **IP:** `192.168.4.1`
*   **DNS:** Captivo (redirige todo a 192.168.4.1)

**Parámetros Configurables:**
1.  **Wi-Fi Station:** SSID y contraseña de la red local.
2.  **MQTT Broker:** Host (`mqtt.seguimiento360.com`), Puerto (443), Username, Password.
3.  **Device ID:** Identificador único del Hub (opcional, se genera automáticamente si está vacío).
4.  **OTA Enabled:** Habilitar/deshabilitar actualizaciones remotas.

**Tecnología:**
*   Servidor HTTP embebido en ESP32 (Arduino Core / ESP-IDF).
*   HTML/CSS/JS minificado almacenado en SPIFFS/LittleFS.
*   Credenciales guardadas en NVS (encriptadas con AES-128).

---

## 6. Esquema de Datos y Tópicos

### 📡 Tópicos MQTT

| Tópico | Dirección | Payload | Descripción |
| :--- | :--- | :--- | :--- |
| `s360/telemetry/{hub_id}` | Hub → Cloud | JSON | Telemetría GPS y sensores |
| `s360/alarm/{hub_id}` | Hub → Cloud | JSON | Eventos de alarma (intrusión, caída) |
| `s360/heartbeat/{hub_id}` | Hub → Cloud | JSON | Latido cada 60s (estado online) |
| `s360/command/{hub_id}` | Cloud → Hub | JSON | Comandos (reboot, config, OTA) |
| `s360/config/{hub_id}` | Cloud → Hub | JSON | Configuración remota (thresholds) |

### 📄 Ejemplo de Payload (Telemetría)
```json
{
  "device_id": "hub_001",
  "timestamp": 1729612345,
  "gps": {
    "lat": 40.416775,
    "lon": -3.703790,
    "speed": 0,
    "satellites": 8
  },
  "battery": 87,
  "rssi": -65,
  "nodes": [
    {
      "mac": "a0:f2:62:f1:d0:f0",
      "name": "Sensor Garaje",
      "distance": 45,
      "threshold": 50,
      "alarm": false
    }
  ]
}
```

---

## 7. Base de Datos (PostgreSQL + InfluxDB)

### 🗄️ PostgreSQL (Datos Relacionales)

**Tablas Principales:**
```sql
-- Usuarios y Tenants
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    full_name VARCHAR(255),
    role VARCHAR(50) DEFAULT 'customer',
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE tenants (
    id SERIAL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    owner_user_id INTEGER REFERENCES users(id),
    is_active BOOLEAN DEFAULT true
);

-- Dispositivos
CREATE TABLE hubs (
    id SERIAL PRIMARY KEY,
    tenant_id INTEGER REFERENCES tenants(id),
    site_id INTEGER REFERENCES sites(id),
    serial_number VARCHAR(32) UNIQUE NOT NULL,
    device_type VARCHAR(16) DEFAULT 'alarm_hub',
    name VARCHAR(64),
    status VARCHAR(16) DEFAULT 'pending',
    last_seen TIMESTAMP,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE nodes (
    id SERIAL PRIMARY KEY,
    hub_id INTEGER REFERENCES hubs(id) ON DELETE CASCADE,
    mac_address VARCHAR(17) UNIQUE NOT NULL,
    name VARCHAR(64) DEFAULT 'Nodo Sin Nombre',
    location VARCHAR(64) DEFAULT 'Sin Ubicación',
    room VARCHAR(64) DEFAULT 'Sin Habitación',
    distance_threshold INTEGER DEFAULT 50 CHECK (distance_threshold BETWEEN 2 AND 200),
    status VARCHAR(20) DEFAULT 'pending',
    last_seen TIMESTAMP,
    created_at TIMESTAMP DEFAULT NOW()
);
```

### ⏱️ InfluxDB (Series de Tiempo)

**Bucket:** `telemetria` (retención: 90 días)

**Measurement:** `gps_data`
```
Tags: device_id, device_type, tenant_id
Fields: latitude (float), longitude (float), speed (int), battery (int), rssi (int)
Timestamp: Unix epoch (seconds)
```

**Measurement:** `sensor_data`
```
Tags: node_mac, hub_id, sensor_type
Fields: distance (int), temperature (float), humidity (float), alarm (bool)
Timestamp: Unix epoch (seconds)
```

---

## 8. API REST - Endpoints

### 🔐 Autenticación

| Método | Endpoint | Auth | Descripción | Payload |
| :--- | :--- | :--- | :--- | :--- |
| `POST` | `/api/auth/register` | ❌ | Crear usuario + tenant | `{"email":"...","password":"...","full_name":"..."}` |
| `POST` | `/api/auth/login` | ❌ | Login y obtener JWT | `{"email":"...","password":"..."}` |
| `GET` | `/api/auth/profile` | ✅ JWT | Obtener perfil usuario | - |
| `POST` | `/api/auth/logout` | ✅ JWT | Cerrar sesión | - |

### 📡 Dispositivos

| Método | Endpoint | Auth | Descripción | Query Params |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/api/auth/nodes` | ✅ JWT | Listar nodos del tenant | - |
| `GET` | `/api/auth/hubs` | ✅ JWT | Listar hubs del tenant | - |
| `PUT` | `/api/nodes/:id` | ✅ JWT | Actualizar nodo (nombre, threshold) | `{"name":"...","threshold":50}` |
| `POST` | `/api/hubs/:id/reboot` | ✅ JWT | Reiniciar hub remoto | - |

### 📊 Telemetría

| Método | Endpoint | Auth | Descripción | Query Params |
| :--- | :--- | :--- | :--- | :--- |
| `GET` | `/api/telemetry/gps` | ✅ JWT | Obtener historial GPS | `?from=ISO_DATE&to=ISO_DATE&device_id=X` |
| `GET` | `/api/telemetry/sensors` | ✅ JWT | Obtener datos de sensores | `?node_mac=XX:XX:XX...&hours=24` |

**Respuesta Ejemplo (Login):**
```json
{
  "success": true,
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyX2lkIjo3LCJlbWFpbCI6InRlc3RAY2llcnJlLmNvbSIsInJvbGUiOiJjdXN0b21lciIsInRlbmFudF9pZCI6NSwiaWF0IjoxNzI5NjEyMzQ1LCJleHAiOjE3Mjk2OTg3NDV9.abc123...",
  "user": {
    "id": 7,
    "email": "test@cierra.com",
    "full_name": "Test Cierre",
    "role": "customer",
    "tenant_id": 5
  }
}
```

---

## 9. URLs y Puntos de Acceso

### 🌐 Subdominios (Cloudflare Tunnel)

| Subdominio | Servicio Interno | Puerto | Uso |
| :--- | :--- | :--- | :--- |
| `seguimiento360.com` | Nginx (Portal Web) | 80 | Portal de usuario y documentación |
| `mqtt.seguimiento360.com` | Mosquitto (WSS) | 8083 | Conexión WebSocket segura para ESP32 |
| `app.seguimiento360.com` | Traccar | 8082 | Panel de rastreo GPS |
| `nodered.seguimiento360.com` | Node-RED | 1880 | Editor de flujos (requiere auth) |
| `grafana.seguimiento360.com` | Grafana | 3000 | Dashboards de telemetría |
| `gotify.seguimiento360.com` | Gotify | 80 | Notificaciones push |
| `uptime.seguimiento360.com` | Uptime-Kuma | 3001 | Monitoreo de servicios |

### 🔐 Credenciales por Defecto (CAMBIAR EN PRODUCCIÓN)
*   **PostgreSQL:** `admin` / `admin` (usuario: `nodered_app`)
*   **Grafana:** `admin` / `admin`
*   **Node-RED Editor:** Configurado en `settings.js` (obligatorio)
*   **Mosquitto:** Sin autenticación por defecto (recomendado: usuario/pass en `.env`)

---

## 10. Guía de Despliegue y Mantenimiento

###  Requisitos del Servidor
*   **SO:** Ubuntu Server 22.04 LTS
*   **RAM:** 4GB mínimo (8GB recomendado)
*   **CPU:** 2 vCPU
*   **Disco:** 50GB SSD
*   **Red:** Conexión estable 10+ Mbps

###  Instalación Paso a Paso

1.  **Clonar repositorio:**
    ```bash
    git clone https://github.com/khalifa63/seguimiento360-alarma.git
    cd seguimiento360-alarma
    ```

2.  **Configurar variables de entorno:**
    ```bash
    cp .env.example .env
    nano .env  # Editar con valores reales
    ```

3.  **Generar JWT Secret:**
    ```bash
    openssl rand -hex 32
    # Copiar el resultado en JWT_SECRET en .env
    ```

4.  **Configurar Node-RED:**
    ```bash
    cp nodered/data/settings.js.example nodered/data/settings.js
    nano nodered/data/settings.js  # Editar dbConfig.password
    ```

5.  **Instalar dependencias en Node-RED:**
    ```bash
    docker exec nodered npm install pg jsonwebtoken bcryptjs
    ```

6.  **Levantar servicios:**
    ```bash
    docker compose up -d
    ```

7.  **Configurar Cloudflare Tunnel:**
    *   Acceder a [Cloudflare Zero Trust](https://one.dash.cloudflare.com/)
    *   Networks → Tunnels → Create Tunnel
    *   Copiar el token y pegarlo en `CLOUDFLARE_TUNNEL_TOKEN` en `.env`
    *   Reiniciar: `docker restart cloudflared`

8.  **Configurar DNS en Cloudflare:**
    *   Agregar registros CNAME para cada subdominio apuntando al tunnel.

### 🔧 Comandos Útiles

```bash
# Ver logs en tiempo real
docker compose logs -f nodered
docker compose logs -f cloudflared

# Reiniciar servicios
docker compose restart nodered nginx mosquitto

# Backup de PostgreSQL
docker exec postgres pg_dump -U admin seguimiento360_alarmas > backup_$(date +%F).sql

# Ver estado de contenedores
docker compose ps

# Acceder a consola de PostgreSQL
docker exec -it postgres psql -U admin -d seguimiento360_alarmas
```

###  Actualización de Firmware (OTA)
1.  Compilar firmware en PlatformIO/ESP-IDF.
2.  Generar archivo `.bin`.
3.  Subir a servidor web estático o bucket S3.
4.  Publicar comando MQTT en `s360/command/{hub_id}` con URL del firmware.
5.  El Hub descarga, verifica checksum y se reinicia automáticamente.

---

## 11. Seguridad y Recomendaciones

### 🔒 Checklist de Seguridad
- [ ] **Cambiar todas las contraseñas por defecto** (PostgreSQL, Grafana, Node-RED).
- [ ] **Habilitar autenticación en Mosquitto** (usuario/password en `mosquitto.conf`).
- [ ] **Configurar `adminAuth` en Node-RED** (hash bcrypt en `settings.js`).
- [ ] **Usar HTTPS/WSS exclusivamente** (Cloudflare maneja SSL automáticamente).
- [ ] **No exponer puertos en firewall** (solo 22, 80, 443 permitidos en UFW).
- [ ] **Rotar JWT_SECRET cada 90 días** (invalida todos los tokens activos).
- [ ] **Habilitar 2FA en Cloudflare** y cuenta de GitHub.
- [ ] **Backups automáticos diarios** de PostgreSQL y archivos de configuración.

### ️ Hardening del Servidor (UFW)
```bash
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp comment 'SSH'
sudo ufw allow 80/tcp comment 'HTTP (Cloudflare)'
sudo ufw allow 443/tcp comment 'HTTPS (Cloudflare)'
sudo ufw enable
```

###  Monitoreo Recomendado
*   **Uptime-Kuma:** Configurar alertas si algún servicio cae.
*   **Grafana:** Dashboard de uso de CPU/RAM/Disk del servidor.
*   **Logrotate:** Rotar logs de Docker para evitar llenar el disco.

### ⚠️ Troubleshooting Común

| Problema | Causa Probable | Solución |
| :--- | :--- | :--- |
| **Error 502 en `/api`** | Nginx no resuelve `nodered:1880` | Verificar red Docker: `docker network inspect iot-alarma_default` |
| **Cloudflare Tunnel offline** | Token expirado o red caída | `docker logs cloudflared` y renovar token |
| **ESP32 no conecta a MQTT** | Puerto 443 bloqueado o cert inválido | Verificar firewall y que `mqtt.seguimiento360.com` resuelva correctamente |
| **Login redirige al dashboard y vuelve a login** | Token no se guarda en localStorage | Limpiar caché del navegador (F12 → Application → Clear Storage) |
| **Error "password authentication failed"** | Contraseña incorrecta en `settings.js` | Verificar `dbConfig.password` coincide con `POSTGRES_PASSWORD` en `.env` |

---

## 📄 Licencia y Contacto

**Proyecto Privado - Seguimiento 360**  
© 2026 Antonio. Todos los derechos reservados.

**Soporte Técnico:**  
*   Email: antonio.califa@gmail.com
*   Documentación: `/docs` en el repositorio
*   Issues: GitHub Issues (solo para desarrolladores autorizados)

---

*Última actualización: Octubre 2026 | Versión: v1.2.0*

---
