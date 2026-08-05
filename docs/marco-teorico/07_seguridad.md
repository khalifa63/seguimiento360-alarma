# CAPÍTULO 7: SEGURIDAD

## 7.1 Modelo de Defensa en Profundidad

La seguridad del sistema se aborda por capas independientes, de modo que la falla de una no comprometa al conjunto: seguridad fisica (tamper y baterias de respaldo), seguridad de enlace (ESP-NOW cifrado), seguridad de transporte (TLS en el borde), seguridad de identidad (credenciales unicas por dispositivo y JWT por usuario), seguridad de datos (aislamiento multi-inquilino y ACL) y seguridad de operacion (auditoria, monitoreo y endurecimiento de servicios). Cada capa se describe en las secciones siguientes.

## 7.2 Autenticación y Autorización MQTT

### 7.2.1 Estado inicial y transición

La documentacion de infraestructura registra que el broker Mosquitto opera con allow_anonymous true, aceptable para un entorno monousuario inicial pero inaceptable para el modelo multi-inquilino. El plan de transicion es:

    1. Crear el archivo de contraseñas:
       docker exec mosquitto mosquitto_passwd -c /mosquitto/config/passwd admin
    2. Editar mosquitto.conf:
       allow_anonymous false
       password_file /mosquitto/config/passwd
       acl_file /mosquitto/config/acl.conf
    3. Reiniciar: docker compose restart mosquitto

### 7.2.2 Credenciales por dispositivo

Cada dispositivo recibe en la vinculacion un usuario device_<serial> y una contrasena aleatoria de alta entropia, almacenada en el backend como hash en la tabla device_credentials y en el dispositivo en NVS. Las credenciales se rotan automaticamente en cada revinculacion. El super-administrador posee un usuario propio con acceso al arbol completo de administracion.

### 7.2.3 Listas de control de acceso (ACL)

El aislamiento entre inquilinos se impone en el broker mediante ACL por usuario, de modo que ningun dispositivo pueda leer o escribir topicos de otro cliente aunque sus credenciales fueran comprometidas:

    user device_ESP32S3-A1B2C3
    topic readwrite seguimiento360/tenant/T001/site/S01/#
    topic read seguimiento360/tenant/T001/site/S01/hub/H01/command

    user admin
    topic readwrite seguimiento360/#

La PWA no recibe credenciales MQTT: consume eventos por WebSocket autenticado contra Node-RED, reduciendo la superficie de exposicion en el navegador.

## 7.3 Cifrado de Comunicaciones

El trafico externo viaja siempre cifrado: Cloudflare Tunnel realiza terminacion TLS en el borde con certificados gestionados automaticamente, y los dispositivos se conectan por MQTT sobre WebSockets seguros (WSS, puerto 443) y por HTTPS para provisioning y OTA. Esto protege contra interceptacion en redes publicas y funciona detras de NAT y CGNAT sin puertos abiertos en el firewall (UFW permite solo 22, 80 y 443).

El trafico interno entre contenedores de la red Docker viaja sin cifrar por HTTP/MQTT plano, lo cual es aceptable mientras la red del servidor permanezca privada y el acceso fisico y remoto al host este controlado. Como evolucion de Fase 2 se documenta la habilitacion del listener 8883 de Mosquitto con certificados TLS para cifrado extremo a extremo opcional, y la revision periodica de los certificados del borde.

## 7.4 Seguridad de la API y las Aplicaciones

El backend valida en cada peticion un token JWT firmado que porta user_id, tenant_id y rol, con expiracion de 24 horas y renovacion mediante refresh token. Las contrasenas de usuario se almacenan con hash bcrypt. Toda consulta de datos se filtra por el tenant del token, imponiendo aislamiento aun si una regla de negocio fallara. Se habilitan cabeceras de seguridad HTTP (X-Frame-Options, Content-Security-Policy, Strict-Transport-Security), limitacion de tasa en los endpoints de autenticacion para mitigar fuerza bruta, y validacion estricta de entradas en vinculacion de dispositivos (serie y codigo de activacion de un solo uso).

## 7.5 Seguridad del Firmware y del Enlace Local

El enlace ESP-NOW entre sensores y hub usa el cifrado nativo del protocolo con claves PMK/LMK, donde la LMK se deriva de un secreto por inquilino: los nodos de un cliente resultan ilegitimos e ilegibles para equipos de otro cliente. El estado critico (identidad, credenciales, estado de armado) se persiste en NVS con integridad verificada al arranque; ante corrupcion el dispositivo entra en modo fabrica seguro. El hub y el rastreador incorporan tamper fisico que genera alerta de sabotaje. Como evolucion de Fase 2 se habilitaran Secure Boot y cifrado de flash en el ESP32 para proteger el firmware y las claves ante acceso fisico al dispositivo.

## 7.6 Actualizaciones OTA Seguras

El canal OTA verifica doblemente cada imagen: hash de integridad y firma criptografica con clave privada custodiada fuera de los dispositivos. El bootloader con rollback activo revierte automaticamente si la nueva version no confirma arranque saludable, evitando dejar inoperativa la flota ante una actualizacion defectuosa o maliciosa. La descarga ocurre por HTTPS desde el dominio propio y con jitter aleatorio para evitar tormentas de descargas. Ningun dispositivo acepta imagenes no firmadas, lo que impide la instalacion de firmware adulterado aun con acceso local al equipo.

## 7.7 Endurecimiento de Servicios Existentes

La documentacion de infraestructura identifica servicios con credenciales por defecto o sin autenticacion. El plan de endurecimiento obligatorio antes del lanzamiento comercial es:

| Servicio | Estado documentado | Accion requerida |
|---|---|---|
| Mosquitto | allow_anonymous true | Autenticacion y ACL (7.2) |
| Node-RED | Sin autenticacion | adminAuth con hash bcrypt en settings.js |
| Grafana | admin/admin | Cambio de contrasena y usuarios por rol |
| Gotify | admin/admin | Cambio de contrasena y tokens por cliente |
| InfluxDB | admin123 en .env | Contrasena fuerte y tokens de minimo privilegio |
| Traccar | admin debil | Contrasena fuerte y usuarios por cliente |
| Uptime-Kuma | Sin proteger | Autenticacion habilitada |
| .env | Tokens en texto plano | Restriccion de permisos y rotacion periodica |

Para Node-RED se genera el hash con bcryptjs y se configura adminAuth en settings.js; para Grafana se usa grafana-cli admin reset-admin-password. Estas acciones se incorporan al checklist de despliegue del Capitulo 8.

## 7.8 Análisis de Vulnerabilidades

Se aplica un modelo de amenazas sobre los flujos criticos, con sus mitigaciones:

| Amenaza | Ejemplo en el sistema | Mitigacion |
|---|---|---|
| Suplantacion | Dispositivo falso publica alarma | Credenciales unicas, ACL, registro de series en fabrica |
| Alteracion | Comando de desarme interceptado | TLS en transporte, JWT en API, ACK y SEQ en ESP-NOW |
| Repudio | Cliente niega una accion | Auditoria en device_audit_log y log de alarmas |
| Divulgacion | Lectura de telemetria ajena | ACL por inquilino, filtrado por tenant, cifrado |
| Denegacion de servicio | Inundacion de mensajes MQTT | Proteccion DDoS de Cloudflare, limites de tasa, watchdog |
| Elevacion de privilegios | Cliente accede a otro tenant | ACL en broker y filtrado estricto en backend |
| Sabotaje fisico | Apertura de carcasa o corte de bateria | Tamper, bateria de respaldo, testamento MQTT |

El riesgo residual se gestiona con monitoreo continuo (Uptime-Kuma, watchdog de heartbeats) y con el procedimiento de respuesta: ante credencial comprometida, revocacion y reemision inmediata desde el panel de administracion, quedando el dispositivo antiguo expulsado del broker.

## 7.9 Resumen del Capítulo

El diseño de seguridad establece defensa en profundidad con autenticacion MQTT por dispositivo y ACL por inquilino, transporte cifrado en el borde, API protegida con JWT y bcrypt, enlace ESP-NOW cifrado por cliente, OTA firmado con rollback, y un plan concreto de endurecimiento de los servicios existentes que elimina credenciales por defecto y accesos anonimos antes del lanzamiento comercial. El analisis de amenazas documenta las mitigaciones adoptadas y el procedimiento de respuesta ante incidentes.
