// Service Worker de Seguimiento 360
//
// IMPORTANTE: al cambiar este archivo, sube el numero de VERSION.
// El portal lo comprueba y fuerza la actualizacion si no coincide.
// Si no se sube, los navegadores pueden tardar dias en actualizarlo,
// y mientras tanto los avisos se verian como JSON sin procesar.

const VERSION = '2026-10-27-3';

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

// Patrones de vibracion segun la gravedad del evento.
const VIBRACION = {
  critica: [400, 150, 400, 150, 400, 150, 400, 150, 400, 150, 800],
  media:   [250, 120, 250, 120, 250, 120, 500],
  aviso:   [200, 100, 200]
};

const TITULO = {
  critica: '\uD83D\uDEA8 ALARMA',
  media:   '\u26A0\uFE0F AVISO',
  aviso:   'Seguimiento 360'
};

// El portal pregunta la version para saber si hay una nueva
self.addEventListener('message', (event) => {
  const datos = event.data || {};

  if (datos.tipo === 'version') {
    const respuesta = { tipo: 'version', version: VERSION };
    // Si vino un MessageChannel, responder por el puerto (event.source
    // es null en ese caso); si no, por el canal normal.
    if (event.ports && event.ports.length > 0) {
      event.ports[0].postMessage(respuesta);
    } else if (event.source && event.source.postMessage) {
      event.source.postMessage(respuesta);
    }
  }

  if (datos.tipo === 'activar-ya') {
    self.skipWaiting();
  }
});

self.addEventListener('push', (event) => {
  let datos = {};
  let crudo = '';

  try {
    crudo = event.data ? event.data.text() : '';
  } catch (e) {
    crudo = '';
  }

  // Se intenta leer como JSON. Si no lo es, se usa como texto plano:
  // asi una version antigua del servidor nunca rompe el aviso.
  try {
    datos = crudo ? JSON.parse(crudo) : {};
  } catch (e) {
    datos = { body: crudo };
  }

  // Respaldo: si por lo que sea llegara un JSON en texto, se detecta
  if (!datos.title && typeof datos.body === 'string' && datos.body.trim().startsWith('{')) {
    try {
      const alterno = JSON.parse(datos.body);
      if (alterno.title || alterno.body) datos = alterno;
    } catch (e) { /* se deja como esta */ }
  }

  const gravedad = datos.gravedad || 'aviso';

  const opciones = {
    body: datos.body || 'Alarma detectada',
    icon: '/portal/icono-192.png',
    badge: '/portal/icono-192.png',
    vibrate: datos.vibrate || VIBRACION[gravedad] || VIBRACION.aviso,
    requireInteraction: gravedad === 'critica',
    // Web Push no permite sonido propio: se usa el del sistema
    silent: false,
    tag: datos.tag || 's360-evento',
    renotify: true,
    data: {
      url: datos.url || '/portal/dashboard.html',
      gravedad: gravedad,
      version: VERSION
    }
  };

  event.waitUntil(
    self.registration.showNotification(datos.title || TITULO[gravedad], opciones)
  );
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const destino = (event.notification.data && event.notification.data.url) || '/portal/';

  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((lista) => {
      for (const cliente of lista) {
        if (cliente.url.includes('/portal/') && 'focus' in cliente) {
          return cliente.focus();
        }
      }
      if (self.clients.openWindow) return self.clients.openWindow(destino);
    })
  );
});

// Si el navegador renueva la suscripcion por su cuenta, se avisa al portal
self.addEventListener('pushsubscriptionchange', (event) => {
  event.waitUntil(
    (async () => {
      try {
        const clientes = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
        clientes.forEach((c) => c.postMessage({ tipo: 'suscripcion-cambiada' }));
      } catch (e) { /* el portal la volvera a registrar al abrirse */ }
    })()
  );
});
