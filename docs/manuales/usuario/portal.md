# Tu cuenta en Seguimiento 360

**Guia del portal web.**

---

## Que es esto

Es la pagina donde ves tus dispositivos y lo que ha pasado en tu casa.

Se entra desde cualquier navegador, tambien desde el movil:

    https://app.seguimiento360.com

---

## Crear tu cuenta

1. Entra en la pagina y pulsa **Registrarse**.
2. Rellena:
   - **Correo**: el tuyo. Lo usaras para entrar.
   - **Contrasena**: minimo **8 caracteres**. Anotala bien.
   - **Nombre**: como quieres que te llamemos.
   - **Telefono** (opcional).
3. Pulsa **Crear cuenta**.

Ya puedes entrar. **No hace falta confirmar el correo** por ahora.

> **Ojo con la contrasena.** Todavia no hay forma de recuperarla desde la web.
> Si la pierdes, tendras que pedir ayuda. Guardala en un sitio seguro.

---

## Entrar

1. Escribe tu correo y contrasena.
2. Pulsa **Entrar**.

La sesion dura **24 horas**. Pasado ese tiempo, te pedira la contrasena otra vez.
Es normal.

### Si olvidaste la contrasena

Todavia **no hay recuperacion automatica**. Contacta con el servicio tecnico.

---

## Que ves al entrar

Tu **panel**, con tus dispositivos.

Cada dispositivo aparece como una tarjeta con:

- Su nombre (el que le pusiste).
- Donde esta: ubicacion y habitacion.
- Si esta **activo** o **pendiente**.
- Cuando fue la ultima vez que dio senal.

---

## Anadir un dispositivo

Cuando instalas un sensor nuevo, hay que decirle a la web que existe.

1. Busca la **MAC** del sensor. Es un numero asi:

       a0:f2:62:f1:d0:f0

   Viene en la etiqueta de abajo del aparato.

2. En el panel, pulsa **Anadir dispositivo**.
3. Rellena:

   | Campo | Que poner |
   |---|---|
   | **MAC** | el numero de la etiqueta, con sus dos puntos |
   | **Nombre** | como quieres llamarlo ("Sensor Garaje") |
   | **Ubicacion** | la casa o el sitio ("Casa Principal") |
   | **Habitacion** | donde esta ("Garaje") |
   | **Distancia** | a cuantos centimetros quieres que avise |

4. Pulsa **Guardar**.

A partir de ahi aparece en tu panel.

> **Importante:** que el sensor funcione **no** significa que ya este en la web.
> Si no lo anades, funciona pero no lo ves. Es lo primero que hay que hacer tras
> instalarlo.

---

## Cambiar el nombre o el sitio

Pulsa **Editar** en la tarjeta del dispositivo y cambia lo que necesites.

Puedes cambiar:

- El nombre.
- La ubicacion y la habitacion.
- **A cuantos centimetros avisa.**

Al guardar, el cambio **llega al aparato**: el sensor empieza a usar la nueva
distancia sin que tengas que tocarlo.

---

## Borrar un dispositivo

Pulsa **Borrar** en su tarjeta.

Se quita de la lista. **El aparato sigue funcionando**, pero deja de aparecer en
la web. Si lo quieres de vuelta, vuelve a anadirlo.

---

## Recibir avisos en el movil

Cuando algo pasa en tu casa, el sistema puede avisarte al movil. Para eso hay
que **activar las notificaciones** en cada aparato donde quieras recibirlas.

### Como activarlas

Lo mas facil es desde el propio panel:

1. Entra al portal y mira arriba del todo.
2. Si ves un recuadro que dice **Activa las notificaciones de alarma**, pulsa el
   boton azul.
3. El navegador te preguntara si quieres permitir las notificaciones. Pulsa
   **Permitir**.
4. Listo. El recuadro desaparece y ya recibes los avisos.

Si no ves ese recuadro, es que en ese aparato ya estan activadas.

### Si le diste a "Bloquear" sin querer

El navegador **no vuelve a preguntar** si una vez dijiste que no. Hay que
abrirlo a mano:

1. Fijate en el **candado** que hay a la izquierda de la direccion web.
2. Pulsalo y busca **Notificaciones**.
3. Cambialo a **Permitir**.
4. Recarga la pagina y pulsa otra vez el boton azul.

Tambien tienes una **pagina de ayuda** que te dice exactamente que falta. Se
abre desde el boton **? Ayuda** que hay arriba a la derecha, en cualquier
pagina del portal.

Esa pagina te muestra cuatro cosas:

- Si diste permiso a las notificaciones
- Si el aviso esta registrado en este aparato
- Si tienes la sesion abierta
- Un boton para activarlo todo otra vez

Si algo sale en rojo, ese es el problema.

### Que avisos llegan

No todo avisa. Solo lo importante:

| Que pasa | Te avisa | Como suena |
|---|---|---|
| Alarma del sensor (ALRM) | Si, urgente | Vibra fuerte y no se quita hasta que la tocas |
| Puerta, movimiento | Si, normal | Vibra un poco |
| El sensor sigue vivo (latido) | No | Nada |

**Los latidos no avisan.** Son solo para que el sistema sepa que el sensor
funciona. Si avisaran cada 10 minutos, serian molestos.

### Como se ve el aviso

    🚨 ALARMA — Garaje
    Sensor Ultrasonico Garaje · 7 cm · bateria 95%

Sale la habitacion en el titulo y los datos en el mensaje.

Si tocas el aviso, se abre el panel para que veas que ha pasado.

### Preguntas sobre los avisos

**¿Necesito tener el navegador abierto?**
En Android, no: los avisos llegan aunque tengas el navegador cerrado y el
telefono bloqueado. En iPhone, la app tiene que estar instalada, pero una vez
instalada no hace falta tenerla abierta. En el computador si hace falta que el
navegador este abierto, aunque sea minimizado.

**¿Funciona en iPhone?**
Si, con dos diferencias. Primero, **hay que instalar la app** (en Safari:
Compartir > Anadir a pantalla de inicio): sin eso no llegan los avisos.
Segundo, **no vibra**: Apple no permite que una web haga vibrar el telefono.
El aviso se ve igual, pero sin vibracion.

**¿Por que no suena una sirena?**
Los navegadores no dejan poner sonidos propios. Se usa el sonido de aviso que
tengas puesto en el telefono. Si lo tienes en silencio, no sonara, pero si
vibrara.

**¿Por que no me vibra?**
Porque el telefono esta en modo "No molestar", o porque lleva mucho rato sin
tocarse y Android lo tiene dormido. Tocalo y vuelve a intentarlo.

**¿Puedo recibirlos en varios aparatos?**
Si. Activa las notificaciones en cada uno. El movil y el computador pueden
recibir el mismo aviso a la vez.

**Si borro los datos del navegador, ¿dejo de recibirlos?**
Si. Vuelve a entrar al portal y activalos otra vez con el boton azul.

## Instalar la app en el movil

El portal se puede instalar como una aplicacion, con su icono en la pantalla
de inicio. **Es la forma recomendada de usarlo**, y en iPhone es obligatorio
para recibir los avisos.

### Por que conviene instalarlo

- Se abre a pantalla completa, sin la barra del navegador
- Arranca mas rapido
- **En iPhone, sin esto no llegan las notificaciones**
- Android deja de marcar los avisos como "posible spam"

---

### Android (Chrome)

1. Abre el portal en **Chrome**.
2. Toca el menu de los **tres puntos** (arriba a la derecha).
3. Elige **Instalar aplicacion** (o **Anadir a pantalla de inicio**).
4. Confirma. Aparece el icono en tu pantalla de inicio.
5. **Abre la app desde ese icono**, no desde Chrome.

### iPhone y iPad (Safari)

En iPhone **es obligatorio** instalarlo: Safari no entrega notificaciones a una
pagina abierta en el navegador.

1. Abre el portal en **Safari** (tiene que ser Safari, no Chrome).
2. Toca el boton **Compartir**: el cuadro con la flecha hacia arriba, abajo.
3. Desliza y elige **Anadir a pantalla de inicio**.
4. Ponle nombre y toca **Anadir**.
5. **Abre la app desde el icono nuevo**, no desde Safari.
6. Dentro de la app, entra y pulsa **Activar notificaciones**.

**Importante en iPhone:** si abres el portal en Safari y pulsas Activar
notificaciones, parecera que funciona, pero **los avisos no llegaran**. Hay que
abrir la app instalada.

---

### Desinstalarla

En Android: manten pulsado el icono y elige **Desinstalar** o **Quitar**.
En iPhone: manten pulsado el icono, **Eliminar app**, y confirma.

No borra tu cuenta ni tus dispositivos: solo quita el acceso directo.

## Preguntas frecuentes

**Me registre pero no me llego ningun correo.**
Es normal: todavia no se envian correos de confirmacion. Tu cuenta ya funciona.

**Olvide la contrasena.**
No hay recuperacion automatica todavia. Pide ayuda al servicio tecnico.

**Cuantos dispositivos puedo tener?**
Hasta 20 por cada hub.

**Puedo verlo desde el movil?**
Si, la pagina se adapta.

**Cada cuanto se actualiza?**
Al abrir la pagina. **Hay que recargar para ver los cambios**, todavia no se
actualiza sola.

**Puedo tener dos casas?**
Si, pero cada una necesita su propio hub.

---

## Lo que todavia no hace

Para que no lo busques:

- **No envia correos**: ni de confirmacion, ni de recuperacion.
- **No se actualiza sola**: hay que recargar para ver novedades.
- **No muestra un historial de eventos** en la web. Los eventos se guardan, pero
  la pantalla de historial esta a medio construir.
- **No hay vista por hub**: se ven todos los dispositivos juntos, no agrupados
  por su hub.

---

## Si necesitas ayuda

Ten a mano:

1. Tu correo de la cuenta.
2. El nombre del dispositivo que da problemas.
3. Que ves en la pantalla (activo, pendiente, o si no aparece).
