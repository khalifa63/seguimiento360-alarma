/* Seguimiento 360 - Portal. Utilidades compartidas. */
export const API = '';

export const Sesion = {
  get token() { return localStorage.getItem('s360_token'); },
  get usuario() {
    try { return JSON.parse(localStorage.getItem('s360_user') || 'null'); }
    catch (e) { return null; }
  },
  guardar(token, usuario) {
    localStorage.setItem('s360_token', token);
    localStorage.setItem('s360_user', JSON.stringify(usuario));
  },
  cerrar() {
    this.cerrarSilencioso();
    location.href = '/portal/';
  },
  /* El token dura 24 h. Si ya vencio, se borra aqui mismo para no dejar
     basura en el navegador: si no, el usuario vuelve y sigue "logueado"
     hasta que alguna llamada falle. */
  vigente() {
    const t = this.token;
    if (!t) return false;
    let ok = false;
    try {
      const partes = t.split('.');
      if (partes.length === 3) {
        const cuerpo = JSON.parse(atob(partes[1].replace(/-/g, '+').replace(/_/g, '/')));
        ok = cuerpo.exp ? cuerpo.exp * 1000 > Date.now() : true;
      }
    } catch (e) { ok = false; }
    if (!ok) this.cerrarSilencioso();
    return ok;
  },

  /* Borra la sesion sin redirigir (para cuando ya estamos redirigiendo). */
  cerrarSilencioso() {
    localStorage.removeItem('s360_token');
    localStorage.removeItem('s360_user');
  }
};

export async function api(ruta, opciones) {
  const o = opciones || {};
  const cabeceras = { 'Content-Type': 'application/json' };
  if (o.auth !== false && Sesion.token) {
    cabeceras['Authorization'] = 'Bearer ' + Sesion.token;
  }
  let res;
  try {
    res = await fetch(API + ruta, {
      method: o.method || 'GET',
      headers: cabeceras,
      body: o.body ? JSON.stringify(o.body) : undefined
    });
  } catch (e) {
    throw new Error('Sin conexion con el servidor. Revisa tu internet.');
  }
  let datos = null;
  try { datos = await res.json(); } catch (e) { /* cuerpo vacio */ }

  /* Un 401 en las rutas de acceso significa "credenciales incorrectas",
     no "sesion caducada". Solo cerramos sesion si ya habia una que
     presumiamos valida. */
  const esAcceso = /^\/api\/auth\/(login|register)$/.test(ruta);
  if (res.status === 401 && !esAcceso) {
    Sesion.cerrar();
    throw new Error('Sesion expirada. Vuelve a entrar.');
  }
  if (!res.ok) {
    const err = new Error((datos && (datos.error || datos.message)) || ('Error ' + res.status));
    err.status = res.status;
    throw err;
  }
  return datos;
}

export function esc(v) {
  if (v === null || v === undefined) return '';
  return String(v).replace(/[&<>"']/g, c => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
  ));
}

/* "hace 3 min" a partir de una fecha ISO */
export function hace(iso) {
  if (!iso) return 'nunca';
  const seg = Math.floor((Date.now() - new Date(iso).getTime()) / 1000);
  if (isNaN(seg)) return 'nunca';
  if (seg < 0) return 'ahora';
  if (seg < 60) return 'hace ' + seg + ' s';
  const min = Math.floor(seg / 60);
  if (min < 60) return 'hace ' + min + ' min';
  const h = Math.floor(min / 60);
  if (h < 24) return 'hace ' + h + ' h';
  return 'hace ' + Math.floor(h / 24) + ' d';
}

export function fecha(iso) {
  if (!iso) return '-';
  const d = new Date(iso);
  if (isNaN(d)) return '-';
  return d.toLocaleString('es-CO', {
    day: '2-digit', month: '2-digit', year: 'numeric',
    hour: '2-digit', minute: '2-digit', second: '2-digit'
  });
}

/* Nombre legible y color por tipo de evento */
export const EVENTOS = {
  ALRM:  { txt: 'Alarma',        cls: 'p-err'  },
  GAS:   { txt: 'Gas',           cls: 'p-err'  },
  SMOKE: { txt: 'Humo',          cls: 'p-err'  },
  FIRE:  { txt: 'Fuego',         cls: 'p-err'  },
  PANIC: { txt: 'Panico',        cls: 'p-err'  },
  SOS:   { txt: 'SOS',           cls: 'p-err'  },
  MOV:   { txt: 'Movimiento',    cls: 'p-warn' },
  DOOR:  { txt: 'Puerta',        cls: 'p-warn' },
  SEIS:  { txt: 'Sismico',       cls: 'p-warn' },
  HUM:   { txt: 'Humedad',       cls: 'p-warn' },
  HEAR:  { txt: 'Latido',        cls: 'p-gris' }
};
export function evento(tipo) {
  return EVENTOS[tipo] || { txt: tipo || '?', cls: 'p-gris' };
}

/* Cabecera comun. Marca la pestana activa. */
export function cabecera(activa) {
  const u = Sesion.usuario || {};
  const p = [
    ['/portal/dashboard.html', 'Panel',    'panel'],
    ['/portal/nodos.html',     'Nodos',    'nodos'],
    ['/portal/eventos.html',   'Eventos',  'eventos']
  ];
  let h = '<header class="barra"><div class="marca">🛡️ Seguimiento 360</div><nav>';
  for (const [url, txt, id] of p) {
    h += '<a href="' + url + '"' + (id === activa ? ' class="act"' : '') + '>' + txt + '</a>';
  }
  h += '<span style="color:var(--txt2);font-size:.85rem;margin:0 6px;">'
     + esc(u.full_name || u.email || '') + '</span>';
  // Acceso al diagnostico de notificaciones: util cuando el usuario
  // no recibe los avisos y no sabe por que.
  h += '<a href="/portal/diagnostico.html" class="ayuda" title="Diagnóstico de notificaciones">'
     + '<span class="ayuda-icono">?</span><span class="ayuda-texto">Diagnóstico</span></a>';
  h += '<button class="salir" id="btn-salir">Salir</button>';
  h += '</nav></header>';
  return h;
}

/* Exige sesion. Devuelve false si redirige. */
export function exigirSesion() {
  if (!Sesion.vigente()) { location.href = '/portal/'; return false; }
  return true;
}

export function conectarSalir() {
  const b = document.getElementById('btn-salir');
  if (b) b.addEventListener('click', () => Sesion.cerrar());
}


/* ------------------------------------------------------------------
   Registro del dispositivo para notificaciones push

   Se ejecuta solo si el usuario tiene sesion iniciada y ya concedio
   el permiso de notificaciones. El token del navegador se envia al
   servidor para asociarlo a este usuario.
   ------------------------------------------------------------------ */
function urlBase64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - base64String.length % 4) % 4);
  const base64 = (base64String + padding).replace(/-/g, '+').replace(/_/g, '/');
  const raw = atob(base64);
  const salida = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) salida[i] = raw.charCodeAt(i);
  return salida;
}

export async function registrarPushDelNavegador() {
  try {
    if (!('serviceWorker' in navigator) || !('PushManager' in window)) return;

    // Sin sesion no hay a quien asociar el dispositivo
    if (!Sesion.token) return;

    const permiso = Notification.permission;
    if (permiso === 'denied') return;

    const reg = await navigator.serviceWorker.ready;

    // Si aun no hay suscripcion, se pide el permiso al usuario
    let sub = await reg.pushManager.getSubscription();
    if (!sub) {
      if (permiso !== 'granted') {
        const pedido = await Notification.requestPermission();
        if (pedido !== 'granted') return;
      }
      // La clave publica VAPID viaja en el propio HTML
      const clave = document.querySelector('meta[name="vapid-public"]');
      if (!clave) return;
      sub = await reg.pushManager.subscribe({
        userVisibleOnly: true,
        applicationServerKey: urlBase64ToUint8Array(clave.content)
      });
    }

    await fetch('/api/fcm/register', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': 'Bearer ' + Sesion.token
      },
      body: JSON.stringify({ subscription: sub })
    });
  } catch (e) {
    // Si falla, no se interrumpe la navegacion del usuario
    console.warn('No se pudo registrar el dispositivo para notificaciones:', e.message);
  }
}

// Se intenta al cargar y cuando la pagina vuelve a primer plano
registrarPushDelNavegador();
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) registrarPushDelNavegador();
});


/* ------------------------------------------------------------------
   Actualizacion del Service Worker

   Chrome no actualiza el Service Worker hasta que se cierran todas
   las pestanas del sitio, y a veces tarda dias. Mientras tanto, los
   avisos push pueden verse sin procesar (JSON crudo).

   Aqui se comprueba la version cada vez que se abre el portal y, si
   no coincide con la que corre, se fuerza la actualizacion.
   ------------------------------------------------------------------ */
const VERSION_SW_ESPERADA = '2026-10-27-3';

async function comprobarVersionServiceWorker() {
  if (!('serviceWorker' in navigator)) return;

  try {
    const reg = await navigator.serviceWorker.getRegistration();
    if (!reg) return;

    // Preguntar la version al Service Worker activo
    const version = await new Promise((resolver) => {
      const canal = new MessageChannel();
      const temporizador = setTimeout(() => resolver(null), 2000);

      canal.port1.onmessage = (ev) => {
        clearTimeout(temporizador);
        if (ev.data && ev.data.tipo === 'version') resolver(ev.data.version);
        else resolver(null);
      };

      const destino = reg.active || reg.waiting || reg.installing;
      if (!destino) { clearTimeout(temporizador); resolver(null); return; }
      destino.postMessage({ tipo: 'version' }, [canal.port2]);
    });

    if (version === VERSION_SW_ESPERADA) return;

    // Si no hubo respuesta (null), el Service Worker puede ser antiguo y no
    // saber responder. Se actualiza, pero sin recargar en bucle.
    const sinRespuesta = version === null;

    console.warn('Service Worker desactualizado (' + version + ' != ' + VERSION_SW_ESPERADA + '). Actualizando…');
    await reg.update();

    // Si hay uno en espera, activarlo ya
    if (reg.waiting) {
      reg.waiting.postMessage({ tipo: 'activar-ya' });
    }

    // Recargar una sola vez para que el nuevo tome el control.
    // Si no hubo respuesta, se espera mas antes de recargar y solo una vez.
    const marca = 'sw_recargado_' + VERSION_SW_ESPERADA;
    if (!sessionStorage.getItem(marca)) {
      sessionStorage.setItem(marca, '1');
      setTimeout(() => location.reload(), sinRespuesta ? 2500 : 800);
    }
  } catch (e) {
    // Si falla, no se interrumpe la navegacion
    console.warn('No se pudo comprobar la version del Service Worker:', e.message);
  }
}

// Al recibir avisos del propio Service Worker
navigator.serviceWorker.addEventListener('message', (event) => {
  if (event.data && event.data.tipo === 'suscripcion-cambiada') {
    registrarPushDelNavegador();
  }
});

comprobarVersionServiceWorker();


/* ------------------------------------------------------------------
   Aviso de notificaciones en el panel

   Un usuario nuevo puede no tenerlas activadas: bien porque rechazo el
   permiso, bien porque nunca llego a aceptarlo.

   Chrome solo pregunta una vez. Si el usuario dijo "no", no vuelve a
   ofrecerlo nunca, asi que hay que darle una via manual.
   ------------------------------------------------------------------ */

/* Estado de las notificaciones en este dispositivo */
export async function estadoNotificaciones() {
  if (!('Notification' in window) || !('serviceWorker' in navigator)) {
    return { soportado: false };
  }

  const permiso = Notification.permission;
  let suscrito = false;

  try {
    const reg = await navigator.serviceWorker.getRegistration();
    if (reg && reg.pushManager) {
      const sub = await reg.pushManager.getSubscription();
      suscrito = !!sub;
    }
  } catch (e) {
    suscrito = false;
  }

  return { soportado: true, permiso, suscrito };
}

function avisoDescartado() {
  try { return sessionStorage.getItem('s360_aviso_push') === 'descartado'; }
  catch (e) { return false; }
}

function descartarAviso() {
  try { sessionStorage.setItem('s360_aviso_push', 'descartado'); } catch (e) {}
}

/* Pinta el aviso dentro del contenedor indicado */
export async function avisoNotificaciones(idContenedor) {
  const caja = document.getElementById(idContenedor);
  if (!caja) return;

  const estado = await estadoNotificaciones();

  if (!estado.soportado) return;
  if (avisoDescartado()) return;

  // Si ya esta todo listo, se quita el aviso si estuviera puesto
  if (estado.permiso === 'granted' && estado.suscrito) {
    caja.innerHTML = '';
    return;
  }

  let titulo, texto, boton;

  if (estado.permiso === 'denied') {
    titulo = 'Notificaciones bloqueadas';
    texto = 'Tu navegador tiene bloqueadas las notificaciones para este sitio. ' +
            'Abrelas en el candado de la barra de direcciones para recibir las alarmas.';
    boton = 'Ya las active';
  } else {
    titulo = 'Activa las notificaciones de alarma';
    texto = 'Ahora mismo no recibirás avisos en este dispositivo. ' +
            'Actívalas para que te llegue la alarma aunque no tengas el portal abierto.';
    boton = 'Activar notificaciones';
  }

  caja.innerHTML =
    '<div class="aviso-push" id="aviso-push">' +
      '<div class="aviso-push-icono">🔔</div>' +
      '<div class="aviso-push-texto">' +
        '<strong>' + titulo + '</strong>' +
        '<span>' + texto + '</span>' +
      '</div>' +
      '<div class="aviso-push-acciones">' +
        '<button class="btn-aviso-principal" id="btn-aviso-activar">' + boton + '</button>' +
        '<button class="btn-aviso-secundario" id="btn-aviso-cerrar" title="Ocultar">✕</button>' +
      '</div>' +
      '<div class="aviso-push-estado" id="aviso-push-estado"></div>' +
    '</div>';

  document.getElementById('btn-aviso-cerrar').addEventListener('click', () => {
    descartarAviso();
    caja.innerHTML = '';
  });

  document.getElementById('btn-aviso-activar').addEventListener('click', async () => {
    const estadoCaja = document.getElementById('aviso-push-estado');
    const botonActivar = document.getElementById('btn-aviso-activar');
    botonActivar.disabled = true;
    const decir = (t) => { if (estadoCaja) estadoCaja.textContent = t; };

    try {
      if (Notification.permission === 'denied') {
        // Puede haberlo activado en el candado: se vuelve a comprobar
        const ahora = await estadoNotificaciones();
        if (ahora.permiso === 'denied') {
          decir('Sigue bloqueado. Ábrelo en el candado de la barra de direcciones.');
          botonActivar.disabled = false;
          return;
        }
      }

      decir('Pidiendo permiso…');
      const permiso = await Notification.requestPermission();
      if (permiso !== 'granted') {
        decir('No se concedió el permiso.');
        botonActivar.disabled = false;
        return;
      }

      decir('Registrando este dispositivo…');
      await registrarPushDelNavegador();

      const nuevo = await estadoNotificaciones();
      if (nuevo.suscrito) {
        decir('¡Listo! Ya recibirás los avisos de alarma.');
        setTimeout(() => { caja.innerHTML = ''; }, 2500);
      } else {
        decir('No se pudo completar. Prueba desde la página de diagnóstico.');
        botonActivar.disabled = false;
      }
    } catch (e) {
      decir('Error: ' + e.message);
      botonActivar.disabled = false;
    }
  });
}


/* ------------------------------------------------------------------
   Recarga forzada de la hoja de estilos

   Chrome en Android (y sobre todo la PWA instalada) guarda el CSS
   mucho tiempo. Subir el ?v= en el HTML no basta si el propio HTML
   esta cacheado.

   Aqui se comprueba si la hoja cargada tiene la version esperada y,
   si no, se recarga anadiendo un parametro nuevo. Solo se hace una
   vez por sesion, para no crear un bucle.
   ------------------------------------------------------------------ */
const VERSION_CSS_ESPERADA = '20261027k';

async function forzarCssActualizado() {
  try {
    const enlace = document.querySelector('link[rel="stylesheet"][href*="estilo"]');
    if (!enlace) return;

    // Si la URL ya lleva la version esperada, no hay nada que hacer
    if (enlace.href.includes('v=' + VERSION_CSS_ESPERADA)) return;

    // Evitar bucles: solo un intento por sesion
    if (sessionStorage.getItem('css_recargado') === VERSION_CSS_ESPERADA) return;
    sessionStorage.setItem('css_recargado', VERSION_CSS_ESPERADA);

    const url = new URL(enlace.href);
    url.searchParams.set('v', VERSION_CSS_ESPERADA);
    enlace.href = url.toString();
  } catch (e) { /* si falla, no se interrumpe nada */ }
}

forzarCssActualizado();


/* ------------------------------------------------------------------
   Boton para instalar la aplicacion

   Chrome en Android avisa con "beforeinstallprompt" cuando el portal
   se puede instalar. Ese aviso se guarda y se ofrece al usuario como
   un boton, en lugar de esperar a que lo encuentre en el menu.

   En iPhone ese evento no existe: alli la instalacion se hace a mano
   desde Compartir > Anadir a pantalla de inicio.
   ------------------------------------------------------------------ */

let eventoInstalacion = null;

export function esIOS() {
  const ua = navigator.userAgent || '';
  const iPhone = /iPhone|iPad|iPod/i.test(ua);
  const iPadModerno = /Macintosh/i.test(ua) && navigator.maxTouchPoints > 1;
  return iPhone || iPadModerno;
}

export function estaInstalada() {
  return window.navigator.standalone === true ||
         window.matchMedia('(display-mode: standalone)').matches ||
         window.matchMedia('(display-mode: fullscreen)').matches;
}

/* Guarda el evento para poder lanzarlo cuando el usuario pulse el boton */
window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault();
  eventoInstalacion = e;
  // Si el boton ya esta pintado, se muestra
  const zona = document.getElementById('zona-instalar');
  if (zona) avisoInstalar('zona-instalar');
});

window.addEventListener('appinstalled', () => {
  eventoInstalacion = null;
  const zona = document.getElementById('zona-instalar');
  if (zona) zona.innerHTML = '';
});

/* Pinta el aviso de instalacion dentro del contenedor indicado */
export function avisoInstalar(idContenedor) {
  const caja = document.getElementById(idContenedor);
  if (!caja) return;

  // Si ya esta instalada, no se muestra nada
  if (estaInstalada()) { caja.innerHTML = ''; return; }

  // En Android solo se muestra si el navegador ofrecio instalarla
  if (!esIOS() && !eventoInstalacion) { caja.innerHTML = ''; return; }

  const enIOS = esIOS();

  caja.innerHTML =
    '<div class="aviso-push" id="aviso-instalar">' +
      '<div class="aviso-push-icono">📲</div>' +
      '<div class="aviso-push-texto">' +
        '<strong>Instala la aplicación</strong>' +
        '<span>' + (enIOS
          ? 'Para recibir las alarmas en iPhone hay que instalarla: toca Compartir ' +
            '(el cuadro con la flecha), elige <strong>Añadir a pantalla de inicio</strong> ' +
            'y abre la app desde ese icono.'
          : 'Tenla a mano en la pantalla de inicio: se abre más rápido, sin la barra ' +
            'del navegador, y los avisos dejan de marcarse como spam.') +
        '</span>' +
      '</div>' +
      (enIOS ? '' :
      '<div class="aviso-push-acciones">' +
        '<button class="btn-aviso-principal" id="btn-instalar">Instalar</button>' +
        '<button class="btn-aviso-secundario" id="btn-instalar-cerrar" title="Ocultar">✕</button>' +
      '</div>') +
      '<div class="aviso-push-estado" id="instalar-estado"></div>' +
    '</div>';

  const cerrar = document.getElementById('btn-instalar-cerrar');
  if (cerrar) {
    cerrar.addEventListener('click', () => {
      try { sessionStorage.setItem('s360_aviso_instalar', 'descartado'); } catch (e) {}
      caja.innerHTML = '';
    });
  }

  const boton = document.getElementById('btn-instalar');
  if (boton) {
    boton.addEventListener('click', async () => {
      if (!eventoInstalacion) return;
      boton.disabled = true;
      const estado = document.getElementById('instalar-estado');
      try {
        eventoInstalacion.prompt();
        const eleccion = await eventoInstalacion.userChoice;
        if (estado) {
          estado.textContent = eleccion.outcome === 'accepted'
            ? '¡Instalada! Búscala en tu pantalla de inicio.'
            : 'Puedes instalarla más tarde desde el menú del navegador.';
        }
        eventoInstalacion = null;
        if (eleccion.outcome === 'accepted') {
          setTimeout(() => { caja.innerHTML = ''; }, 3000);
        }
      } catch (e) {
        if (estado) estado.textContent = 'No se pudo abrir el instalador.';
      }
      boton.disabled = false;
    });
  }
}
