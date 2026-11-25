# Continuidad de sesiones

Este es el punto de entrada operativo para retomar el proyecto sin depender de la memoria del chat. Actualizarlo al cerrar cada sesión y contrastar sus datos con GitHub y los registros privados antes de actuar. No guardar secretos, direcciones privadas ni evidencia de usuarios.

## Mensaje para iniciar una sesión nueva

Copia este mensaje y adjunta este archivo, o pide leer su versión actual en GitHub:

> Continuamos el proyecto Seguimiento360. Lee primero `AGENTS.md` y `docs/desarrollo/CONTINUIDAD.md` del repositorio `https://github.com/khalifa63/seguimiento360-alarma`, rama `main`. Verifica el directorio de trabajo, estado Git y versión remota antes de actuar; este archivo puede estar desactualizado si es una copia descargada. Distingue versión de fuentes de versión instalada. Resume brevemente el estado y retoma el primer pendiente seguro. No muevas ni descartes clones antiguos. No flashees, abras el puerto serie, captures ni reinicies servicios sin mi autorización. Responde en español y de forma corta. Actualiza el documento de continuidad al cerrar la sesión.

## Autoridad y ubicación

- GitHub: `https://github.com/khalifa63/seguimiento360-alarma`, rama `main`.
- Proyecto: Seguimiento360; la cámara Arduino es un subproyecto del mismo repositorio.
- Procedimiento y estructura: [Versionado, fuentes y flasheo](../infraestructura/versionado-y-flasheo.md).
- Copia local utilizada hasta esta revisión: `seguimiento360-alarma-actualizado` bajo el workspace de la sesión. Está prevista su sustitución operativa por `proyectos/seguimiento360-alarma`, sin borrar ni mover copias anteriores. **Migración aún no ejecutada.**
- Clon canónico del servidor creado: `~/proyectos/seguimiento360-alarma`, origen GitHub por SSH, limpio en `60593a2` al crearlo el 25 de noviembre. Se crearon directorios privados `config`, `releases`, `devices` y `backups` bajo `~/operacion/seguimiento360-alarma`, sin copiar secretos. Tras publicar esta revisión debe actualizarse por fast-forward y verificarse de nuevo. No usar el clon antiguo divergente ni el prototipo activo como fuentes de autoridad.

## Versión instalada conocida: cámara

- Fuente: commit `1da2d990d7a66c9550bcdc665b493da7cc29ded4`.
- Validación documentada: commit `60593a2ac4ab7a38d88e221d3dabe8d750b10816`.
- Aplicación desplegada: SHA-256 `06acd91c8bb92b0e51e302fb746cd43039cb6d204d2ccab4958814afd29650f5`.
- Instalación: 24 de noviembre de 2026, aplicación en `0x10000`, sin borrado general ni actualización de particiones. Arduino-ESP32 `3.3.11`; compilación exitosa con Arduino CLI `1.2.2`.
- Respaldo completo privado de 4 MiB existente y hash verificado. Ubicación y detalles de dispositivo se conservan en el servidor privado, no en Git.
- Usuario confirmó foto, ráfaga visible de cinco y clip funcional. Servidor confirmó 5/5 y 30/30, sin comando pendiente tras el clip.
- Incidencias: montaje de microSD intermitente; primera ráfaga demoró unos cinco minutos; apertura serie pudo reiniciar la cámara; acceso HTTP directo no confirmado. Ver [detalle de validación](../firmware/esp32cam-evidence.md).
- No hay lectura independiente posterior del rango flasheado documentada: la evidencia de escritura es la verificación de hash de esptool más las pruebas funcionales. No atribuirle retrospectivamente el nuevo proceso.

## Validación del entorno Ubuntu: 25 de noviembre de 2026

Evidencia comunicada por el operador: clon canónico en rama main, HEAD y origin/main coincidentes en `b3057a729929a416a7b50db5887a069689407f7d`, árbol limpio. Directorios privados de configuración, releases, dispositivos y respaldos con permisos 700. Los clones antiguos se conservan.

- Herramientas: Git 2.56.0, Python 3.14.4, PyYAML 6.0.3, Node 22.22.1, npm 9.2.0, ESP-IDF 5.2.2, Arduino CLI 1.2.2, core ESP32 3.3.11, Docker 29.9.2 y Compose 5.6.1.
- Esptool del entorno IDF: 4.12.0. El futuro instalador requiere 5.3.1 en entorno separado; no actualizar el entorno IDF globalmente.
- Validaciones locales: diff sin errores visibles, manifiestos válidos, 9 pruebas de manifiestos y 19 de cámara aprobadas. La ejecución de check-docs no mostró salida; no se registró su código de retorno por separado.
- Compilación real con plantilla pública copiada fuera del clon: build y verify de camera_release.py exitosos; manifiesto privado y cinco binarios, aplicación de 1281104 bytes, imagen combinada de 4194304 bytes. Release de prueba de 50 MB. Árbol Git limpio después de compilar. No instalar este artefacto.
- CI del PR 6: docs y cámara aprobadas; HUB, buzzer, reed y pánico compilados; ultrasónico falló por campo distance_cm ausente en espnow_payload_v1_t. Firma OTA de CI aislada; no implica firma de producción. Pendiente revisar contrato y corregir en rama independiente.
- Estado: adecuación de desarrollo del servidor comprobada al commit indicado; versiones instaladas, migración de registros privados, copia canónica del PC, instalador seguro y aceptación física pendientes. La evidencia histórica de cámara del 24 de noviembre no cambia. Sin reinicios ni flasheos.

## Trabajo histórico: 25 de noviembre de 2026

Objetivo aprobado: GitHub como autoridad, una carpeta por proyecto, continuidad entre sesiones y procedimiento único trazable de compilación/flasheo. Se aprobó mantener el monorepo y no mover copias antiguas ni flashear la placa.

### Hecho

- Inventario inicial de clones y despliegue, sin modificar servicios.
- Diseño de estructura, identidad de versiones, registros y controles de seguridad.
- Validación histórica de foto/ráfaga/clip conservada; no se hicieron nuevas capturas ni reinicios en esta etapa.

### Pendientes originales (históricos; consultar validación posterior)

1. Publicar esta revisión aprobada y verificar coincidencia con `main` remoto. La herramienta `scripts/camera_release.py` y sus 19 pruebas unitarias ya están implementadas; la versión final pasó las 19 en Ubuntu, con herramientas simuladas y sin hardware. Eso no equivale a una compilación real.
2. Actualizar por fast-forward el nuevo clon canónico del servidor y validar una compilación real con plantilla, sin secretos ni puerto serie.
3. Crear y verificar la copia canónica nueva del PC sin tocar las copias anteriores. El nuevo clon y directorios operativos del servidor ya se crearon; actualizar esta sección tras verificar la revisión publicada.
4. Implementar y revisar el instalador automatizado conforme a FLASH-01 a FLASH-08. No está implementado todavía; no usar el nuevo diseño como autorización de escritura.
5. Validar el proceso de compilación con plantilla en el servidor, sin secretos ni puerto serie. La prueba física requiere ventana y autorización nuevas.
6. Revisar por separado cambios locales/commits no publicados en clones antiguos. No mezclarlos ni descartarlos en esta tarea.

## ENG-001 — cierre de preparación documental (registro histórico) (25 de noviembre de 2026)

El responsable autorizó cerrar la etapa de organización general. Entregables preparados: plan maestro, catálogo, normalización de ubicación documental con inventario de legado, fichas de seis componentes, manifiesto y matriz de compatibilidad en borrador, manuales preliminares, criterios de aceptación, riesgos y estructura de registros privados.

- Clon canónico del servidor: se confirmó actualización local de main a 2254660 y recuperación del primer commit de ingeniería 327b93d en la rama docs/plan-maestro-ingenieria, con árbol limpio. Los clones antiguos no se han reconciliado.
- Último antecedente de esta revisión: 9ff17e1. Los commits posteriores al primer commit local requieren confirmar publicación y sincronización; no asumir que main los contiene.
- Validación estática: 137 Markdown inventariados, sin enlaces rotos en los 11 Markdown revisados en el último bloque y sin rutas inexistentes en la navegación. No se ejecutó el validador completo sobre esta revisión, Markdownlint ni MkDocs. No se certifican firmware, hardware ni seguridad.
- Estado: preparación documental terminada; cierre de integración condicionado a publicación, revisión y verificaciones automatizadas satisfactorias. Los manuales siguen siendo borradores. No se autorizan despliegues.
- Próximo paso seguro: confirmar rama remota y HEAD, ejecutar git diff --check, scripts/check-docs.sh, Markdownlint y MkDocs; revisar resultados antes de fusionar. Después iniciar ENG-002, continuidad independiente por componente. Conservar pendientes históricos de cámara y clones.

Consultar [registro de cierre ENG-001](../ingenieria/ENG-001.md).

## ENG-002 — continuidad por subproyecto (registro histórico)

ENG-001 se integró en main mediante PR 3, commit 251d4f6, con Markdownlint, check-docs y MkDocs estricto aprobados en ejecución 45660228480. La sincronización local posterior sigue sin comprobarse. Consultar el [índice de subproyectos](subproyectos/README.md) y [ENG-002](../ingenieria/ENG-002.md) para retomar un área. Los pendientes físicos y operativos anteriores se mantienen.

## ENG-003 — versionado y trazabilidad (registro histórico)

ENG-002 se integró en main mediante PR 4, commit b14da120, con CI documental aprobado (45660950832). ENG-003 prepara convenciones de versiones y validación de manifiestos; consultar [ENG-003](../ingenieria/ENG-003.md). La sincronización local y las versiones instaladas siguen pendientes de comprobación.

## Preparación ENG-004 a ENG-010 (registro histórico)

ENG-003 integrado mediante PR 5, commit 29df7f6; CI documental y de manifiestos aprobadas. ENG-004 tiene PR 6 independiente para compilaciones. Los preparativos de electrónica, CAD, fabricación, infraestructura, banco y aceptación se registran en [estado de hoja de ruta](../ingenieria/ESTADO_ENG.md). No constituyen aceptación física. No se ha comprobado sincronización del servidor.

## Reanudar con seguridad

1. Leer `AGENTS.md`, este documento y el procedimiento enlazado.
2. Obtener directorio real de trabajo; verificar origen Git, rama, estado y HEAD remoto. Si este documento y Git difieren, registrar la diferencia antes de continuar.
3. Revisar commits recientes y registros privados de instalaciones. El HEAD del repositorio puede ser más nuevo que la aplicación instalada y eso es normal.
4. Retomar el primer pendiente comprobando su estado real. No repetir compilaciones o trabajos en segundo plano todavía activos.
5. No abrir puerto serie, reiniciar servicios, capturar, borrar flash ni formatear SD sin acuerdo. Las pruebas unitarias no requieren hardware.

## Cierre de cada sesión

Actualizar: fecha, copia canónica real, versión instalada y evidencia, cambios publicados, pruebas ejecutadas, pendientes y siguiente paso seguro. No escribir «todo listo» si faltan pruebas o migración. Mantener este documento en un commit publicado para que la próxima sesión pueda encontrarlo desde GitHub. El identificador de ese mismo commit se obtiene con Git, no se inserta una autorreferencia imposible dentro del archivo.
