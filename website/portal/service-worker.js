// Service Worker de Seguimiento 360
// Recibe los avisos push y los muestra como notificacion del sistema.

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

// Patrones de vibracion segun la gravedad del evento.
// Se repiten si el aviso es critico, para que cueste ignorarlo.
const VIBRACION = {
  critica: [400, 150, 400, 150, 400, 150, 400, 150, 400, 150, 800],
  media:   [250, 120, 250, 120, 250, 120, 500],
  aviso:   [200, 100, 200]
};

// Titulo por defecto cuando el aviso no trae uno
const TITULO = {
  critica: '\uD83D\uDEA8 ALARMA',
  media:   '\u26A0\uFE0F AVISO',
  aviso:   'Seguimiento 360'
};

self.addEventListener('push', (event) => {
  let datos = {};
  try {
    datos = event.data ? event.data.json() : {};
  } catch (e) {
    datos = { body: event.data ? event.data.text() : '' };
  }

  const gravedad = datos.gravedad || 'aviso';

  const opciones = {
    body: datos.body || 'Alarma detectada',
    icon: '/portal/icono-192.png',
    badge: '/portal/icono-192.png',

    // Vibracion segun gravedad. El movil debe estar despierto para vibrar.
    vibrate: datos.vibrate || VIBRACION[gravedad] || VIBRACION.aviso,

    // En alarmas graves la notificacion no desaparece hasta que la tocan.
    // En avisos menores se deja que el sistema la retire.
    requireInteraction: gravedad === 'critica',

    // Sonido: se deja el que tenga configurado el usuario en Android.
    // Web Push no permite elegir un archivo de audio propio.
    silent: false,

    // Un tag por tipo de evento: agrupa repeticiones del mismo sensor,
    // pero renotify vuelve a avisar cada vez que llega una nueva.
    tag: datos.tag || 's360-evento',
    renotify: true,

    // A donde ir al tocar la notificacion
    data: {
      url: datos.url || '/portal/dashboard.html',
      gravedad: gravedad
    }
  };

  event.waitUntil(
    self.registration.showNotification(datos.title || TITULO[gravedad], opciones)
  );
});

// Al tocar la notificacion: enfocar el portal si ya esta abierto, o abrirlo.
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const destino = (event.notification.data && event.notification.data.url) || '/portal/';

  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((lista) => {
      for (const cliente of lista) {
        if (cliente.url.includes('/portal/') && 'focus' in cliente) {
          if ('navigate' in cliente && destino && !cliente.url.includes(destino.split('/').pop())) {
            cliente.navigate(destino).catch(() => {});
          }
          return cliente.focus();
        }
      }
      if (self.clients.openWindow) return self.clients.openWindow(destino);
    })
  );
});

// Si el navegador renueva la suscripcion, avisar al servidor
self.addEventListener('pushsubscriptionchange', (event) => {
  event.waitUntil(
    (async () => {
      try {
        const nueva = await self.registration.pushManager.subscribe(
          event.oldSubscription ? event.oldSubscription.options : { userVisibleOnly: true }
        );
        const clientes = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
        clientes.forEach((c) => c.postMessage({ tipo: 'suscripcion-renovada', subscription: nueva }));
      } catch (e) {
        // Si falla, el portal la volvera a registrar al abrirse
      }
    })()
  );
});
