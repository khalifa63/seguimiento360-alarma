# Arquitectura del Hub S360 (Fase 5 - Hardware)

## Rol
El hub es el unico dispositivo alimentado permanentemente del sistema de alarma.
Responsabilidades:
1. Recepcion permanente de frames ESP-NOW de nodos a bateria
2. Conexion MQTT persistente al servidor (WSS + TLS, puerto 443)
3. Ejecucion de comandos arm/disarm (sirena, salidas, LED)
4. Publicacion de eventos de sensores y estado de bateria de nodos
5. Watchdog y LWT para deteccion de caida (anti-sabotaje)

## Tareas FreeRTOS
- task_espnow:   RX ESP-NOW, cola de frames, ACK a nodos
- task_mqtt:     conexion WSS, pub/sub, reconnect exponencial, LWT
- task_logic:    maquina de estados armed/disarmed, debounce, sirena
- task_prov:     SoftAP + portal cautivo (solo modo provisionamiento)
- task_nvs:      persistencia de config y estado de alarma

## Topics MQTT (coherentes con servidor Fase 3.2.3)
hello:    seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/hello
command:  seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/command  (sub, retained)
response: seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/response (pub)
event:    seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/event    (pub)
status:   seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/status   (LWT)

## Provisionamiento (flujo de fabrica a campo)
1. Hub sin config -> SoftAP S360-HUB-<serial> + portal HTTP
2. Usuario ingresa WiFi + claim code
3. Hub valida claim contra /api/claim y recibe credenciales MQTT
   (tabla device_credentials, credenciales por dispositivo)
4. Guarda en NVS, reboot, conecta WSS y publica hello
5. Servidor marca hub active

## Alimentacion
5V USB + respaldo (supercap o 18650) para shutdown gracioso y
mantener sirena ante corte de energia (reporta power_loss como evento).

## MVP (hito 1 de hardware)
- DevKit + LED + boton
- Nodo boton publica frame event -> hub publica a /event
- Hub suscrito a /command: arm enciende LED rojo, disarm verde
- hello al boot confirma hub en PWA
