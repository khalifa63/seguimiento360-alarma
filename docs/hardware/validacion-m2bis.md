# 📄 Reporte de Validación: Milestone M2bis (ESP-NOW APSTA)

**Fecha:** 26 de Agosto de 2026  
**Responsable:** Antonio  
**Versión del Firmware:** v0.5.0-m2bis  
**Hardware:** HUB (ESP32-S3 DevKit) + NODO (ESP32 DevKit V1 / D0WD-V3)

## 1. Objetivo
Validar el enlace inalámbrico ESP-NOW en modo **APSTA** (Access Point + Station simultáneos) en el HUB, cumpliendo con el criterio de aceptación **AT-02**: Latencia de ACK < 50 ms y entrega robusta de frames.

## 2. Arquitectura Validada
- **HUB (ESP32-S3):** Modo `WIFI_MODE_APSTA`. 
  - STA: Conectado a `KHALIFA_R` (Canal 5) para salida MQTT/Internet.
  - AP: `HUB_360_PROD` (Hereda Canal 5 del STA por limitación de radio única).
- **NODO (ESP32):** Modo `WIFI_MODE_STA`. Asociado al AP del HUB para sincronización de canal y enlace ESP-NOW fiable.
- **Protocolo:** Frame estructurado v1 (Magic `0xA5`, Versión `0x01`, CRC16-CCITT, Anti-replay).

## 3. Resultados de la Prueba (50 frames)
| Métrica | Valor Obtenido | Criterio AT-02 | Estado |
|---------|----------------|----------------|--------|
| **Entrega de Frames** | 50 / 50 (100%) | ≥ 80% | ✅ **PASS** |
| **RTT Mínimo** | 11.04 ms | < 50 ms | ✅ **PASS** |
| **RTT Promedio** | 13.55 ms | < 50 ms | ✅ **PASS** |
| **RTT Máximo** | 25.74 ms | < 50 ms | ✅ **PASS** |

## 4. Hallazgos Técnicos Clave
1. **Fix de CRC en Buffer Lineal:** Se corrigió un bug crítico donde el CRC se escribía en el offset 234 del struct packed. Ahora se construye en un buffer lineal de 21 bytes (`header` + `crc`), garantizando que el CRC se transmita en los bytes 19-20.
2. **MAC de Origen en ACK:** En modo APSTA, el HUB responde al ACK usando la MAC de la interfaz AP (`3c:dc:75:62:c2:b1`), no la STA. El NODO valida correctamente este BSSID.
3. **Registro Dinámico de Peers:** El HUB registra automáticamente a los nodos desconocidos en `WIFI_IF_AP` al recibir el primer frame, permitiendo el envío inmediato del ACK inline.

## 5. Recomendación de Optimización (Fase 2)
Fijar el router `KHALIFA_R` en el **Canal 1** (en lugar de Auto/Canal 5). Esto eliminará la interferencia del entorno y reducirá el RTT promedio de ~13.5 ms a ~10 ms, replicando las condiciones ideales de la prueba AT-02 original.

## 6. Conclusión
El subsistema de alarma local (M2bis) cumple sobradamente con los requisitos de baja latencia y alta confiabilidad. El firmware está listo para la integración de la lógica de publicación MQTT (Fase 3).
