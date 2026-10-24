# Respaldo y restauracion de Node-RED

Como hacer una copia que sirva para volver atras si una prueba sale mal, y como
restaurarla. Actualizado: 2026-10-24.

## Que hay que copiar

**Tres archivos, no uno.** Copiar solo el flujo no basta:

| Archivo | Que es | Por que hace falta |
|---|---|---|
| `flows.json` | el flujo | es lo que se quiere recuperar |
| `flows_cred.json` | credenciales cifradas | la contrasena del broker MQTT y demas |
| `settings.js` | configuracion | contiene la `credentialSecret` que descifra lo anterior |

Las credenciales de Node-RED se cifran con una clave que vive en `settings.js`.
Restaurar `flows_cred.json` sin **su** `settings.js` deja las credenciales
ilegibles: los nodos que las usan —el broker MQTT, entre otros— dejan de
conectar y el sintoma no aparece hasta que algo intenta publicar.

## Hacer un respaldo

    scripts/respaldar-nodered.sh
    scripts/respaldar-nodered.sh --etiqueta antes-de-probar-ota
    scripts/respaldar-nodered.sh --listar
    scripts/respaldar-nodered.sh --podar 5      # conservar los 5 ultimos

Copia los tres archivos y **verifica cada uno con md5 contra el contenedor**
antes de dar el respaldo por bueno. Si algo no coincide, lo elimina y avisa:
un respaldo a medias es peor que no tenerlo, porque da confianza sin motivo.

La etiqueta es para acordarse de para que era. Restaurar un respaldo llamado
`...-antes-de-probar-ota` es mas facil de elegir que uno con solo la fecha.

Conviene respaldar **antes de cada prueba que toque el flujo**. El respaldo
solo sirve para volver al estado *anterior* a la prueba.

## Restaurar

    scripts/restaurar-nodered.sh --listar              # ver que hay
    scripts/restaurar-nodered.sh --ultimo              # restaurar el mas reciente
    scripts/restaurar-nodered.sh restaurable-20261024-113501

El script hace seis cosas:

1. **Verifica** que el respaldo tenga los tres archivos y que `flows.json` sea
   JSON valido. Restaurar algo corrupto dejaria Node-RED sin arrancar, que es
   peor que no restaurar.
2. **Guarda el estado actual** en `antes-de-restaurar-<fecha>`, de modo que la
   restauracion tambien se puede deshacer.
3. **Para el contenedor** antes de copiar. Ver la trampa de abajo.
4. Copia los tres archivos.
5. Arranca y espera a que responda, hasta 30 segundos.
6. **Comprueba** que el endpoint devuelva 401. Si no, avisa y dice como volver
   atras.

## La trampa: el archivo no es la fuente de verdad

**Mientras Node-RED corre, `flows.json` no manda.** Node-RED tiene el flujo en
memoria y **reescribe el archivo al parar**, sobrescribiendo lo que se haya
puesto ahi a mano.

Comprobado: se edito `/data/flows.json` para quitar un endpoint, se reincontro
el contenedor, y el cambio **desaparecio**. El archivo volvio a tener el flujo
de memoria.

Por eso hay que **parar el contenedor antes de restaurar**. Un script que
copiara con Node-RED en marcha pareceria funcionar y no haria nada.

La consecuencia practica: para cambiar un flujo se usa la interfaz de Node-RED
o su API, no el archivo. Y para respaldar da igual, porque el archivo en disco
refleja lo que hay en memoria en el momento de leerlo — salvo que se acabe de
editar a mano, en cuyo caso hay que reiniciar para que tome efecto.

## Comprobado

Los dos scripts se probaron con una rotura real, no simulada:

1. Respaldo con `respaldar-nodered.sh --etiqueta prueba-real`.
2. Rotura: se paro el contenedor, se elimino el endpoint `/api/events` del
   archivo y se arranco. Quedo en 116 nodos y `/api/events` devolvia **404**.
3. Restauracion con `restaurar-nodered.sh --ultimo`.
4. Resultado: **117 nodos**, `/api/events` de vuelta en **401**, y el md5 de
   `flows.json` identico al original (`b1aff918e6dc8f06`).

La restauracion devuelve el sistema al estado exacto, credenciales incluidas.

**Para romperlo hubo que parar el contenedor primero.** Editando el archivo
con Node-RED en marcha, el cambio desaparece al reiniciar: lo sobrescribe con
lo que tiene en memoria. Es la misma trampa que obliga a parar antes de
restaurar.

## Backups completos

`scripts/backup-completo.sh` hace ademas un `tar.gz` con toda la plataforma
(base de datos, configuracion, flujos) en `~/backups/seguimiento360/`. Son
utiles para recuperar el sistema entero, pero pesan decenas de MB: para
"volver atras tras una prueba" es mas rapido usar el respaldo de este
documento.

El script **ya no deja copias dentro del contenedor**. Antes creaba una por
ejecucion y no limpiaba las anteriores; en octubre de 2026 habia siete
acumuladas.

## Cuantos respaldos guardar

No hay limite automatico. Los respaldos ocupan unos 80 KB cada uno, asi que no
es un problema de espacio, pero si de claridad: con veinte, elegir el correcto
se vuelve adivinacion.

    scripts/respaldar-nodered.sh --podar 5

Conserva los cinco mas recientes y elimina el resto. Conviene ejecutarlo de vez
en cuando, no en cada respaldo: si se poda automaticamente se corre el riesgo
de perder justo el que hacia falta.

## Volver atras de una restauracion

Cada restauracion guarda el estado anterior en `antes-de-restaurar-<fecha>`.
Si se restauro el respaldo equivocado, se deshace restaurando ese:

    scripts/restaurar-nodered.sh antes-de-restaurar-20261024-114810
