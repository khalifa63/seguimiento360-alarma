# Versionado, fuentes y flasheo reproducible

Este procedimiento evita perder la fuente de una versión instalada y permite retomar el trabajo entre sesiones. GitHub conserva fuentes, especificaciones y decisiones; los secretos, binarios y respaldos permanecen privados. El diseño fue aprobado el 25 de noviembre de 2026. No autoriza nuevos flasheos ni migraciones destructivas.

## Alcance y autoridad

- Repositorio de autoridad: `https://github.com/khalifa63/seguimiento360-alarma`, rama `main`. Una instalación activa NO sustituye al repositorio.
- La cámara sigue como subproyecto en `firmware/esp32cam-evidence/arduino/sd-recovery-firmware/`. El ESP-IDF experimental no reemplaza esta base Arduino.
- Commit completo: identidad de las fuentes. Etiqueta opcional `camera/vMAJOR.MINOR.PATCH`: versión humana, creada sobre un commit publicado, sin mover etiquetas anteriores. Una etiqueta no demuestra qué contiene una placa.
- SHA-256 del binario: identidad del artefacto. Una misma fuente puede producir binarios distintos si cambia el entorno o la configuración privada.
- Identidad de placa y comprobación de lectura/escritura: evidencia de instalación. Un nombre de puerto o la palabra «último» no son una versión.
- `compilado`, `flasheado-verificado` y `validado-usuario` son estados diferentes. Nunca inferir uno del anterior.

## Directorios y responsabilidades

Los nombres siguientes son convenciones por usuario, sin direcciones de red ni credenciales. `WORKSPACE` es el directorio de trabajo autorizado de la sesión; no es la instalación de la herramienta de IA.

| Ubicación | Destino canónico previsto | Uso |
| --- | --- | --- |
| PC | `WORKSPACE/proyectos/seguimiento360-alarma/` | Única copia activa para editar este proyecto |
| Servidor | `~/proyectos/seguimiento360-alarma/` | Clon limpio desde GitHub; ejecutar el proceso aprobado |
| Servidor | `~/operacion/seguimiento360-alarma/config/` | Configuración privada, fuera de Git |
| Servidor | `~/operacion/seguimiento360-alarma/releases/<release-id>/` | Copia de compilación, binarios y manifiesto privados e inmutables |
| Servidor | `~/operacion/seguimiento360-alarma/devices/<alias>/` | Historial privado de instalaciones y validaciones |
| Servidor | `~/operacion/seguimiento360-alarma/backups/<installation-id>/` | Flash completa y datos de restauración |
| Servidor | Despliegues activos existentes | Ejecución de servicios; nunca editar aquí como fuente canónica |

Para otro proyecto se crea otro repositorio y otro nombre bajo `proyectos/`; un componente del monorepo no necesita un clon diferente. No reutilizar directorios de trabajo de otro proyecto.

### Inventario inicial observado

El 25 de noviembre de 2026 se observaron dos clones en el PC: `seguimiento360-alarma-actualizado` limpio en `60593a2` y `seguimiento360-alarma` divergente y con cambios sin confirmar. En el servidor, `~/seguimiento360-alarma` estaba 23 commits por delante de su referencia remota local (referencia potencialmente desactualizada); el prototipo `~/camera-evidence-prototype` no tenía Git. Estos datos son una fotografía, no una instrucción para usarlos como fuentes actuales.

**Transición:** conservar esas copias intactas. No hacer reset, limpiar, mover ni automatizar merges de los clones antiguos. Revisar por separado sus diferencias antes de archivarlos. La copia de desarrollo nueva solo se declara canónica después de verificar origen, HEAD remoto, árbol limpio y ausencia de secretos. El prototipo activo y los respaldos existentes no se mueven como parte de esta transición. Registrar el resultado real en [Continuidad de sesiones](../desarrollo/CONTINUIDAD.md).

## Flujo único de trabajo

1. **Retomar:** leer `AGENTS.md`, [Continuidad de sesiones](../desarrollo/CONTINUIDAD.md), este procedimiento y la [documentación de cámara](../firmware/esp32cam-evidence.md). Comprobar `git status`, `git remote -v`, HEAD local y remoto. Nunca resolver divergencias con `push --force`.
2. **Diseñar:** acordar cambios y pruebas; actualizar especificación antes de cambiar contratos. Editar solo en la copia canónica del PC. No copiar una fuente suelta desde el servidor por encima de Git.
3. **Publicar:** revisar diferencias y secretos; probar, crear commits pequeños y publicar en GitHub. Dejar los pendientes de la sesión en el documento de continuidad, sin credenciales.
4. **Preparar servidor:** confirmar que el clon canónico esté limpio, obtener `main` y registrar el commit completo. Si diverge, detenerse; no hacer reset automático. Las herramientas deben ser las fijadas, no «las últimas».
5. **Compilar una vez:** ejecutar el subcomando `build` de `scripts/camera_release.py` con configuración externa y un directorio privado de release nuevo. El proceso exige fuentes publicadas y genera hashes y manifiesto. Un fallo deja el release como incompleto: no reutilizar su ruta para otro intento.
6. **Verificar artefactos:** ejecutar `verify` sobre ese mismo release. No recompilar entre aprobar y flashear; una recompilación es otro artefacto y requiere otra verificación.
7. **Instalar con ventana aprobada:** seguir el contrato de seguridad de la sección siguiente. Hasta disponer de implementación y pruebas de aceptación del instalador, esta etapa no está automatizada ni autorizada por ejecutar `build` o `verify`.
8. **Aceptar:** comprobar reconexión sin capturas automáticas; solicitar foto, ráfaga y clip al usuario. Registrar resultados reales y pendientes. Publicar solo el resumen saneado y actualizar continuidad.

## Contrato de compilación

Entorno de referencia: Ubuntu servidor, Arduino CLI `1.2.2`, núcleo `esp32:esp32` `3.3.11`, FQBN `esp32:esp32:esp32cam`, propiedades `build.code_debug=3` y `build.warn_data_percentage=75`. Registrar también versión de Python, plataforma y hash del ejecutable CLI cuando estén disponibles; fijar estas versiones no garantiza por sí solo reproducibilidad byte por byte.

La configuración privada `deployment_config.h` se aporta fuera del clon. No registrar sus valores ni su hash en el manifiesto público. La copia de compilación y los binarios contienen datos privados; directorios con permiso `700`, archivos sensibles `600`, sin respaldos en Git ni adjuntos públicos.

El manifiesto privado debe vincular al menos: versión de formato, commit completo, repositorio/FQBN, herramientas, instante UTC, hashes de las fuentes y hashes/tamaños de cada artefacto. No identifica una instalación ni certifica autenticidad criptográfica por sí solo: permisos e integridad del servidor siguen siendo parte de la confianza.

### Uso de la herramienta de release

Desde la raíz del clon limpio y publicado, con el directorio padre privado ya creado:

```bash
python3 -B scripts/camera_release.py build \
  --config "$HOME/operacion/seguimiento360-alarma/config/deployment_config.h" \
  --build-dir "$HOME/operacion/seguimiento360-alarma/releases/ID-NUEVO"
python3 -B scripts/camera_release.py verify \
  --build-dir "$HOME/operacion/seguimiento360-alarma/releases/ID-NUEVO"
python3 -B -m unittest discover -s tests -p test_camera_release.py -v
```

`build` crea un directorio nuevo; `verify` no escribe firmware ni abre el puerto serie. Esta primera versión exige que HEAD sea exactamente el `main` remoto tanto al compilar como al verificar. Si `main` avanzó, se detiene: no interpreta esa condición como corrupción del binario. La verificación histórica de releases independientes de HEAD queda pendiente, y este comando no debe usarse como mecanismo de restauración. No confundir `verify` con verificación física de flash.

## Contrato de flasheo seguro pendiente de automatización

Antes de escribir, el instalador único deberá cumplir estos requisitos. **No usar comandos alternativos ad hoc para eludirlos.** La prueba de un nuevo instalador será simulada primero; la prueba física requiere otra aprobación expresa.

- `FLASH-01`: exigir release completo verificado, autorización explícita, alias de dispositivo, MAC esperada y puerto estable por ID. Verificar esptool `5.3.1`, chip ESP32 y tamaño real de flash. No identificar por puerto solamente.
- `FLASH-02`: bloqueo exclusivo por placa/puerto; detenerse si hay otra instalación o captura activa. La ausencia de cambios en un archivo de estado no demuestra que la cámara esté libre: confirmar también con el usuario.
- `FLASH-03`: leer primero la flash completa, verificar tamaño y SHA-256, guardar metadatos privados y confirmar que se puede restaurar. El respaldo contiene NVS y secretos. No escribir si falla este paso.
- `FLASH-04`: cotejar tabla de particiones leída con el candidato, seleccionar una única partición de aplicación compatible y comprobar límites. Para la tabla validada de esta cámara: aplicación `0x10000`, tamaño `0x300000`, flash 4 MiB. No extrapolar a otras placas ni admitir tablas diferentes silenciosamente.
- `FLASH-05`: escribir solo la aplicación aprobada, preservando NVS, bootloader y tabla. Nunca `erase-flash`. Exigir confirmación de hash de esptool y, como prueba independiente, lectura posterior del rango escrito y comparación con el binario. Registrar diferencias o fallos, nunca éxito por código de salida solamente.
- `FLASH-06`: crear registro durable antes de comenzar: estado `iniciado`, release, commit, hash del binario, identidad física, respaldo y comando sin secretos. Registrar resultados y transición a `flasheado-verificado` solo si las comprobaciones pasan. No sobrescribir el historial; no actualizar la referencia a instalación actual ante fallo. Tras interrupción, exigir inspección y no continuar automáticamente.
- `FLASH-07`: no abrir el monitor serie después de la instalación durante pruebas del usuario. El adaptador puede provocar reset al abrir el puerto. No activar capturas, formateo, recuperación de SD ni reinicios como comprobación automática.
- `FLASH-08`: restauración solo con ventana autorizada, respaldo correspondiente a la misma placa y hash verificado. La restauración de flash completa también restaura el estado antiguo de NVS; advertirlo. Conservar el release que falló para diagnóstico.

### Registro de instalación

El registro privado por dispositivo contendrá: `installation_id`, alias, MAC/chip, instante UTC, commit de fuente, identificador de release y hashes, identidad de configuración privada mediante alias/revisión no secreta, versión de herramientas, copia previa, resultado de lectura posterior y pruebas del usuario. El registro público omite MAC, rutas privadas reales, configuraciones, evidencias y tokens.

La instalación histórica del 24 de noviembre se documenta en la [validación del firmware](../firmware/esp32cam-evidence.md). No fabricar un manifiesto retrospectivo afirmando que se utilizó esta herramienta: todavía no existía. El respaldo, hash verificado por esptool y pruebas de usuario son evidencias históricas diferentes de una lectura independiente posterior.

## Criterios de aceptación del sistema

- `SYS-01`: desde una sesión nueva se localiza una única copia canónica, el commit publicado y la versión instalada sin recurrir a memoria del chat.
- `SYS-02`: build rechaza árbol sucio, HEAD no publicado, herramientas incompatibles y configuración/salida dentro del clon; nunca escribe en una carpeta de release existente.
- `SYS-03`: verify rechaza artefactos modificados, incompletos y rutas que escapen del release.
- `SYS-04`: ninguna prueba automatizada abre el puerto físico ni accede a secretos de producción.
- `SYS-05`: no se declara implementado el contrato FLASH hasta contar con pruebas de fallo, respaldo, particiones, verificación posterior e historial, además de revisión del código.
- `SYS-06`: el índice de documentación y las reglas de agentes apuntan a continuidad; el cierre de sesión distingue completado, pendiente y siguiente paso seguro.

## Estado del despliegue del proceso

Diseño aprobado; herramienta de compilación/verificación implementada y 19 pruebas unitarias aprobadas en Ubuntu, con herramientas simuladas y sin hardware. Creado el nuevo clon canónico del servidor y los directorios operativos privados. Compilación real por esta herramienta, copia canónica nueva del PC, instalador automatizado y prueba física del nuevo proceso pendientes. No sustituye todavía el procedimiento histórico ya realizado ni permite un nuevo flasheo sin autorización.
