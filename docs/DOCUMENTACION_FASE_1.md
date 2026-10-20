
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
