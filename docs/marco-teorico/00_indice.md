# ÍNDICE GENERAL

Capítulo 1. Introducción
  1.1 Planteamiento del problema
  1.2 Justificación
  1.3 Objetivos (general y específicos)
  1.4 Alcance y limitaciones
  1.5 Estructura del documento
  1.6 Metodología de trabajo

Capítulo 2. Estado del Arte
  2.1 Sistemas de alarma convencionales
  2.2 Sistemas de alarma IoT
  2.3 Tecnologías de comunicación inalámbrica (WiFi, ESP-NOW, LoRa, 4G/LTE, comparativa)
  2.4 Protocolos de mensajería IoT (MQTT, CoAP, HTTP/WebSocket, comparativa)
  2.5 Plataformas de microcontroladores (ESP32-S3, ESP32-C3, justificación)
  2.6 Plataformas de rastreo vehicular (Traccar, alternativas, selección)
  2.7 Análisis de trabajos relacionados
  2.8 Resumen del capítulo

Capítulo 3. Análisis de Mercado
  3.1 Tamaño del mercado global
  3.2 Mercado latinoamericano
  3.3 Análisis de competencia (segmentos y comparativa)
  3.4 Segmentación del mercado
  3.5 Propuesta de valor
  3.6 Modelo de negocio
  3.7 Análisis FODA
  3.8 Resumen del capítulo

Capítulo 4. Arquitectura del Sistema
  4.1 Visión general (cuatro capas)
  4.2 Capa de percepción
  4.3 Capa de borde (hub/gateway)
  4.4 Capa de nube (servidor)
  4.5 Capa de aplicación
  4.6 Modelo multi-inquilino y jerarquía de tópicos
  4.7 Flujo de datos end-to-end
  4.8 Resiliencia y autonomía local
  4.9 Decisiones de diseño y justificación
  4.10 Resumen del capítulo

Capítulo 5. Diseño de Hardware
  5.1 Criterios generales de selección
  5.2 Hub central ESP32-S3 (especificaciones, GPIO, alimentación, consideraciones)
  5.3 Nodo de contacto magnético
  5.4 Nodo de movimiento PIR
  5.5 Nodo actuador de sirena
  5.6 Nodos sensores adicionales
  5.7 Rastreador vehicular (arquitectura, alimentación, antenas)
  5.8 Análisis de consumo energético y autonomía
  5.9 Lista de materiales y costos preliminares
  5.10 Resumen del capítulo

Capítulo 6. Diseño de Software
  6.1 Firmware de dispositivos (arquitectura, FSM, ESP-NOW, MQTT, provisioning, OTA)
  6.2 Backend Node-RED (API REST, motor de reglas, notificaciones, Traccar)
  6.3 Frontend PWA (arquitectura, autenticación, dispositivos, alarmas)
  6.4 Bases de datos (PostgreSQL, InfluxDB, retenciones)
  6.5 Resumen del capítulo

Capítulo 7. Seguridad
  7.1 Modelo de defensa en profundidad
  7.2 Autenticación y autorización MQTT (transición, credenciales, ACL)
  7.3 Cifrado de comunicaciones
  7.4 Seguridad de la API y las aplicaciones
  7.5 Seguridad del firmware y del enlace local
  7.6 Actualizaciones OTA seguras
  7.7 Endurecimiento de servicios existentes
  7.8 Análisis de vulnerabilidades
  7.9 Resumen del capítulo

Capítulo 8. Despliegue, Operación y Continuidad
  8.1 Infraestructura del servidor
  8.2 Configuración de servicios y checklist
  8.3 Provisioning de dispositivos
  8.4 Monitoreo y alertas operativas
  8.5 Copias de seguridad del servidor
  8.6 Estrategia de respaldo del desarrollo (3-2-1)
  8.7 Recuperación ante desastres
  8.8 Resumen del capítulo

Capítulo 9. Plan de Negocio
  9.1 Modelo de ingresos
  9.2 Precios y márgenes
  9.3 Estructura de costos e inversión inicial
  9.4 Proyecciones financieras
  9.5 Estrategia de lanzamiento
  9.6 Roadmap de producto a 24 meses
  9.7 Riesgos del negocio y mitigación
  9.8 Resumen del capítulo

Capítulo 10. Conclusiones y Trabajo Futuro
  10.1 Conclusiones
  10.2 Lecciones aprendidas
  10.3 Trabajo futuro

Referencias Bibliográficas
