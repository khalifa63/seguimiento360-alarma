# Protocolo ESP-NOW v1 (nodo <-> hub)

## Frame (max 250 bytes, little endian)
    [0]     version   0x01
    [1]     type      0x01 event | 0x02 ack | 0x03 battery |
                      0x04 tamper | 0x05 heartbeat
    [2..3]  node_id   uint16
    [4]     zone      1..32
    [5]     state     0 ok | 1 trigger | 2 low_batt | 3 tamper
    [6..9]  seq       uint32 (anti-replay, monotonic en NVS del nodo)
    [10..11] crc16    CRC16-CCITT (polinomio 0x1021, init 0xFFFF)

## Seguridad
- PMK global de flota (esp_now_set_pmk)
- LMK por nodo (128-bit) almacenada en pairing y en NVS del hub
- seq monotonic: hub descarta seq <= ultimo visto (anti-replay)
- Pairing: boton fisico en hub 10s -> acepta proximo nodo (modo learn)

## Ciclo del nodo (a bateria)
1. Deep sleep -> wake por interrupcion (PIR/magnet/boton)
2. Enciende radio, envia frame event
3. Espera ack del hub (max 50 ms, 2 reintentos)
4. Reporta battery cada N wakes o si < 20%
5. Deep sleep

## Ciclo del hub (alimentado)
1. RX frame -> valida LMK + seq + CRC
2. Encola a task_logic y ACK inmediato al nodo
3. task_logic publica event por MQTT (QoS 1)
4. Mantiene tabla de nodos con last_seen y battery
   (nodo mudo > 3 heartbeats = evento node_lost)
