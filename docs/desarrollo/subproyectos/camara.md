# Continuidad — Cámara

ID: S360-CAM. Inicialización de registro: 25 de noviembre de 2026.

## Contexto mínimo

- Fuente: `firmware/esp32cam-evidence/arduino/sd-recovery-firmware`.
- Referencia: [documentación del componente](../../ingenieria/componentes/camara.md).
- Base de inventario: `251d4f6dc96dc8cbb2fb2064306989d6778d3a16`; no equivale a versión instalada.
- Rama activa, build y revisión instalada: verificar antes de actuar.

La evidencia histórica de instalación y pruebas del 24 de noviembre se conserva en el [registro general](../CONTINUIDAD.md) y la [guía de cámara](../../firmware/esp32cam-evidence.md). ESP-IDF experimental no sustituye Arduino.

## Pendientes y siguiente paso

El 25 de noviembre se confirmó build real y verify local con plantilla, commit `b3057a729929a416a7b50db5887a069689407f7d`, Arduino CLI 1.2.2 y core ESP32 3.3.11. Cinco binarios; aplicación 1281104 bytes. El manifiesto y artefactos permanecen privados. Las 19 pruebas unitarias también pasaron. No se instaló el release de prueba.

Siguiente paso: implementar y probar de forma simulada el instalador FLASH-01 a FLASH-08; mantener pendientes de microSD y diagnóstico. La versión instalada histórica no cambia.

## Cierre de sesión

Registrar fecha UTC, commit, rama, cambios, pruebas y resultado, referencias a evidencia privada, decisiones, bloqueos y próximo paso. No copiar secretos ni datos operativos privados. Si cambia una interfaz, seguir SDD. No reiniciar ni flashear sin autorización.
