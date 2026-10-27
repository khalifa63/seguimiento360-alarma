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

async function registrarPushDelNavegador() {
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
