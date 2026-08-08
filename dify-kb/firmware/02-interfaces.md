# SPEC-02 — Contratos de Interface del Hub (v0.1)

## HUB-IF-001: Frame ESP-NOW v1
Ver docs/hardware/PROTOCOLO_ESPNOW.md (version 0x01, CRC16-CCITT,
little endian, anti-replay por seq monotonic).

## HUB-IF-002: Topics MQTT
hello:    seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/hello    pub QoS1
command:  seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/command  sub QoS1 (retained)
response: seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/response pub QoS1
event:    seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/event    pub QoS1
status:   seguimiento360/tenant/{tid}/site/{sid}/hub/{hid}/status   LWT

## HUB-IF-003: Payload command
    {"action":"arm|disarm","issued_by":N,"issued_at":ISO8601,"request_id":str}

## HUB-IF-004: Payload response
    {"action":str,"status":"ok|fail"}

## HUB-IF-005: Payload event
    {"node_id":N,"type":N,"zone":N,"state":N,"seq":N}

## HUB-IF-006: Payload hello
    {"serial_number":str,"firmware":str}

## HUB-IF-007: NVS (namespace s360hub)
wifi_ssid:str, wifi_pass:str, serial:str, hub_id:u32, tenant_id:u32,
site_id:u32, mqtt_user:str, mqtt_pass:str, alarm_state:u8,
node_seq_<id>:u32 (anti-replay por nodo)
