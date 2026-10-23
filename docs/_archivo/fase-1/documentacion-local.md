> ⚠️ **Documento histórico.** No es fuente de verdad.
> La documentación vigente está en [docs/README.md](../../README.md).

---

# 📡 Documentación Técnica - Seguimiento 360
## Fase 1: Núcleo IoT, Gestión de Nodos y Actualización Remota (OTA)

**Fecha de creación:** 19 de Octubre, 2026  
**Versión del Documento:** 1.1  
**Responsable:** Antonio  
**Estado:** ✅ **Núcleo Operativo y Validado en Producción**

---

## 1. Resumen Ejecutivo
Se ha desarrollado y validado con éxito la arquitectura base de la plataforma **Seguimiento 360**. El sistema permite la comunicación bidireccional de baja latencia entre nodos sensores ESP32 (vía ESP-NOW), un Hub Gateway ESP32-S3, y un backend en la nube orquestado por Node-RED, utilizando Cloudflare Tunnel para una conectividad segura sin puertos abiertos. 

Se ha logrado implementar con éxito el ciclo completo de:
1. Registro y gestión de nodos sensores.
2. Propagación de configuraciones del backend al Hub vía MQTT.
3. Detección de eventos (alarmas) y notificación push en tiempo real.
4. **Actualización de Firmware Remota (OTA)** segura vía HTTPS.

---

## 2. Arquitectura del Sistema (Flujo de Datos)

```text
[ Nodo Sensor ESP32 ] 
       │ (ESP-NOW, ultra baja potencia)
       ▼
[ Hub Gateway ESP32-S3 ] ──(WiFi/LAN)──> [ Cloudflare Tunnel (WSS/HTTPS) ]
       │                                         │
       │ (Publica eventos ALRM)                  ▼
       └──────────────────────────────> [ Mosquitto MQTT (Docker) ]
                                              │
                                              ▼
                                      [ Node-RED (Lógica de Negocio) ]
                                              │
                      ┌───────────────────────┼───────────────────────┐
                      ▼                       ▼                       ▼
             [ PostgreSQL ]            [ InfluxDB ]             [ Gotify ]
             (Configuración,           (Telemetría,             (Alertas Push
              Usuarios, Audit)          Batería, Eventos)        al Celular)
```

---

## 3. Funcionalidades Implementadas

### 3.1. Firmware del Hub (ESP32-S3)
- **Conectividad:** Conexión MQTT segura vía WebSockets (WSS) a través de Cloudflare.
- **Gestión de Nodos:** Almacena hasta 20 nodos emparejados en NVS. Escucha el tópico `.../node_cfg` para actualizar nombres, ubicaciones y umbrales de distancia en tiempo real sin reiniciar.
- **Router ESP-NOW:** Recibe paquetes de nodos, enriquece el payload con los metadatos del nodo (nombre, ubicación) y lo publica en MQTT.
- **OTA Robusto:** Implementación de `esp_https_ota` con saneamiento de strings y gestión segura de memoria para evitar corrupción durante la descarga del `.bin`.

### 3.2. Backend y APIs (Node-RED)
Se han consolidado y limpiado los flujos para evitar duplicidades y bloqueos. Los endpoints activos son:
- **Autenticación:** `POST /api/auth/register`, `POST /api/auth/login`, `GET /api/auth/profile` (con JWT).
- **Gestión de Nodos (CRUD):** 
  - `GET /api/nodes?hub_id=X`
  - `POST /api/nodes`
  - `PUT /api/nodes/:id` *(Dispara automáticamente la publicación MQTT de configuración al Hub)*.
  - `DELETE /api/nodes/:id`
- **Actualización Remota (OTA):** `POST /api/ota/trigger` *(Valida JWT, busca el Hub en BD, y publica la URL del firmware en el tópico `.../ota_cmd` de forma asíncrona para no bloquear la respuesta HTTP)*.
- **Panel de Alarma:** Gestión de sitios, dispositivos y control de armado/desarmado.

### 3.3. Sistema de Alertas y Telemetría
- **Escucha MQTT:** Suscrito a `seguimiento360/tenant/+/site/+/hub/+/event`.
- **Procesamiento:** Separa el flujo en dos ramas:
  1. **InfluxDB:** Escribe el protocolo de línea para historial de batería y secuencias.
  2. **Gotify:** Filtra eventos críticos (`ALRM`, `GAS`, `SMOKE`, `PANIC`, etc.) y formatea mensajes con prioridad alta para notificación inmediata al celular.

---

## 4. Stack Tecnológico

| Capa | Tecnología | Detalles |
|---|---|---|
| **Dispositivos** | ESP32 / ESP32-S3 | ESP-IDF v5.2.2, FreeRTOS, ESP-NOW, `esp_https_ota` |
| **Comunicación** | MQTT / HTTPS | Mosquitto, Cloudflare Tunnel (WSS/443) |
| **Backend** | Node-RED | Lógica de negocio, APIs REST, JWT, orquestación |
| **Base de Datos** | PostgreSQL | Datos relacionales (Usuarios, Hubs, Nodos, Auditoría) |
| **Series de Tiempo**| InfluxDB v2.x | Telemetría de dispositivos y eventos |
| **Notificaciones** | Gotify | Push notifications a dispositivos móviles |
| **Infraestructura** | Docker Compose | Ubuntu Server, Nginx (archivos estáticos/.bin) |

---

## 5. Desafíos Técnicos Superados (Lecciones Aprendidas)

Durante el desarrollo de esta fase, se resolvieron dos bugs críticos que son vitales documentar para el futuro:

1. **Bloqueo de Flujos en Node-RED:** 
   - *Problema:* Las peticiones `curl` a los endpoints `PUT /api/nodes` y `POST /api/ota/trigger` se quedaban congeladas.
   - *Causa:* El nodo `mqtt out` actúa como "final de línea" y no pasa el objeto `msg` al nodo `http response`. Además, un espacio en blanco invisible en el campo "Topic" del nodo MQTT generaba el error `Invalid topic specified`.
   - *Solución:* Se implementó una bifurcación usando nodos **Link In/Link Out** (o `setTimeout`) para enviar la respuesta HTTP inmediatamente, mientras la publicación MQTT ocurre en segundo plano. Se verificó que el campo "Topic" del nodo MQTT esté **100% vacío** para que use `msg.topic`.

2. **Error `invalid host` en OTA del ESP32:**
   - *Problema:* El Hub recibía la URL correcta por MQTT, pero `esp_https_ota` fallaba inmediatamente con `ESP_FAIL` y `invalid host`.
   - *Causa:* Un bug clásico de **Use-After-Free** en FreeRTOS. La tarea principal (`main.c`) llamaba a `ota_manager_start(url)` y ejecutaba `free(url)` milisegundos después. La tarea OTA, al intentar leer la URL, encontraba memoria corrupta o liberada.
   - *Solución:* Se transfirió la propiedad de la memoria a la tarea OTA. Ahora, `ota_manager.c` es el único responsable de ejecutar `free(url)` una vez que la descarga ha finalizado (con éxito o con error).

---

## 6. Estado de Seguridad Actual y Próximos Pasos

Según la auditoría de la infraestructura, el sistema es funcional pero requiere endurecimiento antes del despliegue masivo.

### 🚨 Acciones Pendientes (Prioridad Alta)
1. **Autenticación en Mosquitto:** Actualmente `allow_anonymous true`. Se debe generar un archivo `passwd` y configurar `allow_anonymous false` en `mosquitto.conf`.
2. **Variables de Entorno:** Asegurar que `JWT_SECRET`, `PG_PASSWORD` y los Tokens de Gotify no estén hardcodeados en Node-RED, sino gestionados vía archivo `.env` de Docker.
3. **Cambio de Credenciales por Defecto:** Cambiar las contraseñas `admin/admin` de Grafana, Gotify e InfluxDB.

### 📈 Acciones de Mejora (Prioridad Media)
1. **Dashboard de Grafana:** Crear un panel visual conectado a InfluxDB para monitorear el estado de conexión de los Hubs y los niveles de batería de los nodos en tiempo real.
2. **Escalado de Campo:** Desplegar 3-5 nodos sensores físicos adicionales para validar la estabilidad de la red ESP-NOW y la gestión de colisiones en el Hub.

---

## 7. Comandos de Referencia Rápida

```bash
# Reiniciar toda la infraestructura
cd ~/iot-alarma && docker compose restart

# Ver logs de Node-RED en tiempo real
docker logs -f nodered

# Probar endpoint de OTA manualmente
curl -X POST http://localhost:1880/api/ota/trigger \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"hub_id": 10}'

# Compilar y flashear firmware del Hub
cd ~/seguimiento360-alarma/firmware/hub
idf.py -p /dev/ttyACM0 flash monitor

---

## 8. Contrato de API para el Frontend (Portal Web)

Para que el portal web (`http://seguimiento360.com`) pueda configurar los nodos, debe consumir los siguientes endpoints del backend (Node-RED). Todos requieren el header: `Authorization: Bearer <JWT_TOKEN>`.

### 8.1. Obtener nodos de un Hub
- **Método:** `GET`
- **URL:** `/api/nodes?hub_id=10`
- **Respuesta (200 OK):** Array de objetos nodo con `id`, `mac_address`, `name`, `location`, `room`, `distance_threshold`, `status`.

### 8.2. Vincular un nuevo nodo
- **Método:** `POST`
- **URL:** `/api/nodes`
- **Payload:** `{"hub_id": 10, "mac_address": "aa:bb:cc:dd:ee:ff", "name": "Sensor X", "location": "Casa", "room": "Sala", "distance_threshold": 50}`
- **Respuesta (201 Created):** `{"success": true, "node": {...}}`

### 8.3. Configurar / Editar un nodo existente (CRÍTICO)
- **Método:** `PUT`
- **URL:** `/api/nodes/:id` (ej. `/api/nodes/1`)
- **Payload:** `{"name": "Sensor Actualizado", "location": "Casa", "room": "Garaje", "distance_threshold": 75}`
- **Respuesta (200 OK):** `{"success": true, "message": "Nodo actualizado en BD y configuración publicada al Hub vía MQTT", ...}`
- *Nota:* Al llamar a este endpoint, el backend actualiza PostgreSQL y dispara automáticamente el mensaje MQTT al Hub ESP32.

- **Payload:**
  ```json
  {
    "hub_id": 10,
    "mac_address": "a0:f2:62:f1:d0:f0",
    "name": "Sensor Nueva Ubicación",
    "location": "Casa Principal",
    "room": "Cocina",
    "distance_threshold": 50
  }
  ```
- **Respuesta Exitosa (201 Created):** `{"success": true, "node": {...}}`

### 8.3. Configurar / Editar un nodo existente (CRÍTICO)
- **Método:** `PUT`
- **URL:** `/api/nodes/1` *(Donde `1` es el ID del nodo)*
- **Payload:** *(Solo se envían los campos que se desean cambiar)*
  ```json
  {
    "name": "Sensor Garaje Actualizado",
    "location": "Casa Principal",
    "room": "Garaje",
    "distance_threshold": 80
  }
  ```
- **Respuesta Exitosa (200 OK):**
  ```json
  {
    "success": true,
    "node": {
      "id": 1,
      "name": "Sensor Garaje Actualizado",
      "location": "Casa Principal",
      "room": "Garaje",
      "distance_threshold": 80,
      "hub_mac": "3cdc7562c2b0",
      "tenant_id": 1,
      "site_id": 1
    },
    "mqtt_topic": "seguimiento360/tenant/1/site/1/hub/3cdc7562c2b0/node_cfg",
    "message": "Nodo actualizado en BD y configuración publicada al Hub vía MQTT"
  }
  ```
- *Nota:* Al llamar a este endpoint, el backend actualiza PostgreSQL y dispara automáticamente el mensaje MQTT al Hub ESP32.

---
*Documento generado como cierre de la Fase 1 de desarrollo. Próxima revisión: Fase 2 (Endurecimiento de Seguridad y Dashboards).*
```

---


---

### 📄 Archivo 2: `website/configurar-nodo.html`
*Copia este bloque y guárdalo en la carpeta `~/iot-alarma/website/` de tu servidor para tener un formulario de prueba funcional inmediatamente.*

```html
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Configurar Nodo - Seguimiento 360</title>
    <script src="https://cdn.tailwindcss.com"></script>
</head>
<body class="bg-gray-100 min-h-screen flex items-center justify-center p-4">
    <div class="bg-white p-8 rounded-lg shadow-lg w-full max-w-md">
        <h1 class="text-2xl font-bold text-gray-800 mb-6 text-center">⚙️ Configurar Nodo Sensor</h1>
        
        <div class="mb-4">
            <label class="block text-sm font-medium text-gray-700 mb-1">API Base URL</label>
            <input type="text" id="apiUrl" value="http://localhost:1880" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500 text-sm">
            <p class="text-xs text-gray-500 mt-1">Usa http://localhost:1880 para pruebas locales o https://seguimiento360.com en producción.</p>
        </div>

        <div class="mb-4">
            <label class="block text-sm font-medium text-gray-700 mb-1">Token JWT</label>
            <input type="password" id="token" placeholder="eyJhbGciOiJIUzI1..." class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
        </div>

        <div class="mb-4">
            <label class="block text-sm font-medium text-gray-700 mb-1">ID del Nodo (ej. 1)</label>
            <input type="number" id="nodeId" value="1" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
        </div>

        <div class="mb-4">
            <label class="block text-sm font-medium text-gray-700 mb-1">Nombre del Nodo</label>
            <input type="text" id="name" value="Sensor Garaje" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
        </div>

        <div class="grid grid-cols-2 gap-4 mb-4">
            <div>
                <label class="block text-sm font-medium text-gray-700 mb-1">Ubicación</label>
                <input type="text" id="location" value="Casa Principal" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
            </div>
            <div>
                <label class="block text-sm font-medium text-gray-700 mb-1">Habitación</label>
                <input type="text" id="room" value="Garaje" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
            </div>
        </div>

        <div class="mb-6">
            <label class="block text-sm font-medium text-gray-700 mb-1">Umbral de Distancia (cm)</label>
            <input type="number" id="threshold" value="75" min="2" max="200" class="w-full px-3 py-2 border border-gray-300 rounded-md focus:outline-none focus:ring-2 focus:ring-blue-500">
        </div>

        <button onclick="updateNode()" class="w-full bg-blue-600 text-white py-2 px-4 rounded-md hover:bg-blue-700 transition duration-200 font-medium">
            💾 Guardar y Enviar al Hub
        </button>

        <div id="result" class="mt-4 p-3 rounded-md text-sm hidden whitespace-pre-line"></div>
    </div>

    <script>
        async function updateNode() {
            const apiUrl = document.getElementById('apiUrl').value.replace(/\/$/, '');
            const token = document.getElementById('token').value;
            const nodeId = document.getElementById('nodeId').value;
            const resultDiv = document.getElementById('result');

            if (!token || !nodeId) {
                showResult('Por favor, completa el Token JWT y el ID del Nodo.', 'error');
                return;
            }

            const payload = {
                name: document.getElementById('name').value,
                location: document.getElementById('location').value,
                room: document.getElementById('room').value,
                distance_threshold: parseInt(document.getElementById('threshold').value)
            };

            resultDiv.className = 'mt-4 p-3 rounded-md text-sm bg-blue-100 text-blue-800 block';
            resultDiv.innerText = '⏳ Enviando configuración...';

            try {
                const response = await fetch(`${apiUrl}/api/nodes/${nodeId}`, {
                    method: 'PUT',
                    headers: {
                        'Authorization': `Bearer ${token}`,
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify(payload)
                });

                const data = await response.json();

                if (response.ok) {
                    showResult(`✅ ¡Éxito!\n${data.message}\nTópico MQTT: ${data.mqtt_topic}`, 'success');
                } else {
                    showResult(`❌ Error (${response.status}): ${data.error || 'Desconocido'}`, 'error');
                }
            } catch (error) {
                showResult(`❌ Error de red: ${error.message}. Verifica la URL de la API y CORS.`, 'error');
            }
        }

        function showResult(message, type) {
            const resultDiv = document.getElementById('result');
            resultDiv.innerText = message;
            resultDiv.className = `mt-4 p-3 rounded-md text-sm whitespace-pre-line block ${
                type === 'success' ? 'bg-green-100 text-green-800' : 'bg-red-100 text-red-800'
            }`;
        }
    </script>
</body>
</html>
```

---
