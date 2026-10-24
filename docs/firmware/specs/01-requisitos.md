# SPEC-01 — Requisitos del Hub S360 (v0.1)

## Funcionales
ID        | Requisito                                                        | Estado       | AT
HUB-F-001 | WiFi STA con reconexion automatica                               | implementado | AT-001
HUB-F-002 | Sesion MQTT persistente WSS+TLS con credenciales por dispositivo | implementado | AT-001
HUB-F-003 | Publicar hello al boot (serial, fw); servidor pasa a active      | implementado | AT-001
HUB-F-004 | Suscribir command (QoS1, retained) y ejecutar arm/disarm         | implementado | AT-002
HUB-F-005 | Reflejar estado en salidas (LED; rele/sirena en 5.2) + response  | implementado | AT-002
HUB-F-006 | RX frame ESP-NOW v1: version, CRC, anti-replay seq + ACK         | implementado | AT-003
HUB-F-007 | Publicar eventos de nodos a /event (QoS1)                        | implementado | AT-003
HUB-F-008 | Tabla de nodos (last_seen, battery); node_lost tras 3 HB         | implementado | AT-004
HUB-F-009 | LWT offline ante caida (anti-sabotaje)                           | implementado | AT-005
HUB-F-010 | NVS: persistir config y estado de alarma                         | implementado | AT-006
HUB-F-011 | Provisioning: SoftAP + portal solo si falta config           | parcial      | AT-019
HUB-F-016 | Claim code para vincular el hub con la cuenta                    | pendiente    | AT-007
HUB-F-012 | OTA seguro (HTTPS, firma, rollback)                              | implementado | AT-015/016/017
HUB-F-013 | Pairing del nodo: aprende la MAC del hub y la guarda en NVS     | implementado | AT-011
HUB-F-014 | Recuperacion ante hub ausente: el nodo se desvincula solo a los 60 s y se re-vincula al reaparecer | implementado | AT-012
HUB-F-015 | El hub solo atiende tramas dirigidas a sus propias MAC (STA o SoftAP) | implementado | AT-012

## No funcionales
ID         | Requisito                                                     | Estado    | AT
HUB-NF-001 | Boot -> MQTT conectado <= 10 s (RF buena)                     | cumplido  | AT-001 (4,85 s)
HUB-NF-002 | Latencia RX ESP-NOW -> publish /event <= 250 ms               | cumplido  | AT-013 (ACK 14 ms, publish 46-92 ms)
HUB-NF-003 | Operacion 24/7: watchdog habilitado, sin reset manual 30 d    | pendiente | AT-009
HUB-NF-004 | Sin secretos quemados en firmware (credenciales via NVS/prov) | cumplido  | review
HUB-NF-005 | Backoff exponencial 1s->60s con jitter (WiFi y MQTT)          | nodo-verif | AT-010

## Retirados
(none)
