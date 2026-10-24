# OTA del hub

Cómo se actualiza el hub por el aire, cómo se firma la imagen y cómo se
verifica. Complementa AT-015 a AT-017 de
[specs/03-pruebas-aceptacion.md](specs/03-pruebas-aceptacion.md).

## Flujo

    1. Se compila y firma el hub. El binario sale de build/seguimiento360_hub.bin

    2. Se copia a ~/iot-alarma/website/ota/ del servidor, que nginx sirve en
       https://seguimiento360.com/ota/ a traves de Cloudflare Tunnel.

    3. Se publica la URL en el topic ota_cmd del hub:

         docker exec mosquitto mosquitto_pub -h localhost \
           -u "$MQTT_NODERED_USER" -P "$MQTT_NODERED_PASSWORD" \
           -t 'seguimiento360/tenant/<T>/site/<S>/hub/<MAC>/ota_cmd' \
           -m 'https://seguimiento360.com/ota/firmware_X.Y.Z.bin'

    4. El hub descarga, verifica la firma, escribe en la particion inactiva y
       reinicia. Al conectar MQTT confirma la imagen y cancela el rollback.

El topic exacto lo imprime el hub al arrancar ("Suscrito a comandos OTA").

## Firma

Toda app debe estar firmada con la clave RSA-3072 del servidor. Un binario sin
firma valida no arranca, ni por OTA ni por cable.

Se usa `SECURE_SIGNED_APPS_NO_SECURE_BOOT` en vez de Secure Boot por hardware:
cubre el riesgo real —un servidor o DNS comprometidos sirviendo firmware
ajeno— **sin quemar eFuses**, de modo que perder la clave no deja el hub
inservible. **No protege contra acceso fisico**: quien tenga el hub en la mano
puede reemplazar el bootloader y saltarse la verificacion.

### La clave

Vive en `~/seguimiento360-claves-ota/` del servidor, con permisos 600. **No
esta en el repo** y no debe estarlo: `SECURITY.md` lo prohibe, y el
`.gitignore` ya bloquea `*.pem`.

Su huella SHA-256, que identifica la instalacion:

    d06f4a6db961d007e42bf60325037d02

Se obtiene con:

    openssl rsa -in ota_signing_key.pem -pubout -outform DER 2>/dev/null \
      | sha256sum | cut -c1-32

### Respaldo de la clave

**Si se pierde la privada, ningun dispositivo acepta ya una actualizacion por
el aire.** La publica va embebida en el firmware desplegado, asi que los
equipos siguen funcionando, pero quedan congelados en su version y solo se
recuperan con un cable USB, uno a uno.

El respaldo es un paquete cifrado con GPG (AES-256) que se genera aparte:

    ota-claves.tar.gz.gpg          claves + manifiesto + procedimiento
    ota-claves.tar.gz.sha256.gpg   huella para comprobar la integridad

Contiene `MANIFIESTO.txt` (que es y para que sirve) y `RESTAURACION.md` (como
devolverlo a un servidor y como comprobar que quedo bien).

Dos reglas:

- **El paquete debe salir del servidor.** Una copia en el mismo equipo no
  protege contra perder el equipo, que es el caso que cubre el respaldo.
- **La frase de cifrado no se guarda junto al paquete.** Si viajan juntas, el
  cifrado no aporta nada.

El respaldo se ha probado descifrandolo, comparando la huella de la clave
restaurada con la de produccion y firmando una imagen con ella: el binario
resultante lo valida la clave publica que ya tienen los dispositivos.

### Rotar la clave

Exige reflashear **todos** los dispositivos por cable: cada uno lleva la clave
vieja embebida y rechaza lo firmado con la nueva. No se puede hacer por OTA.

    espsecure.py generate_signing_key --version 2 --scheme rsa3072 ota_signing_key.pem
    espsecure.py extract_public_key --version 2 \
      --keyfile ota_signing_key.pem ota_signing_key.pub.pem

`extract_public_key` recibe la salida como **argumento posicional**; usar
`--public_key_file` falla con "unrecognized arguments".

Si la clave se pierde, los hubs dejan de aceptar actualizaciones y hay que
flashearlos por cable. Conviene guardar una copia fuera del servidor.

### Verificar un binario antes de publicarlo

    espsecure.py verify_signature --version 2 \
      --keyfile ~/seguimiento360-claves-ota/ota_signing_key.pub.pem \
      build/seguimiento360_hub.bin

La firma anade 4096 bytes al final. La particion `ota_1` es de 1,5 MB y la app
ocupa ~1,18 MB: queda margen, pero conviene vigilarlo.

## Rollback

Si la imagen nueva no llega a confirmarse, el bootloader revierte a la
anterior. **No hay temporizador**: la app arranca en `PENDING_VERIFY` y el
bootloader decide en el **arranque siguiente**. Un firmware que arranca y se
queda vivo sin funcionar no se revierte solo; hace falta un reinicio
(watchdog, corte de luz o reset).

Por eso la confirmacion se hace al **conectar MQTT** y no al final de
`app_main`: asi el rollback cubre tambien el caso "arranca pero no alcanza el
broker".

## Errores que ya han costado tiempo

- **`sdkconfig` obsoleto.** `sdkconfig.defaults` solo se lee al **crear** el
  `sdkconfig`. Un archivo que ya existia conserva sus valores, asi que opciones
  anadidas despues —el rollback, la version, la firma— no se aplican. Si algo
  "esta en los defaults pero no funciona", comparar con
  `grep OPCION sdkconfig`. Para comprobar el estado real, construir desde un
  clon limpio.
- **Puerto de nginx.** El nginx propio escucha en el **8088**; el 80 lo ocupa
  otro contenedor. Consultar el puerto equivocado devuelve 404 y parece que la
  ruta no existe.
- **`ota_cmd` no valida el destinatario en el broker.** Cualquiera con
  credenciales MQTT validas puede pedir una actualizacion. La firma impide
  instalar algo ajeno, pero no evita el intento.
