// Service Worker de Seguimiento 360
// Recibe los avisos push y los muestra como notificacion del sistema.

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

self.addEventListener('push', (event) => {
  let datos = {};
  try {
    datos = event.data ? event.data.json() : {};
  } catch (e) {
    datos = { body: event.data ? event.data.text() : '' };
  }

  const titulo = datos.title || 'Seguimiento 360';
  const opciones = {
    body: datos.body || 'Alarma detectada',
    icon: '/portal/icono-192.png',
    badge: '/portal/icono-192.png',
    vibrate: [200, 100, 200, 100, 200, 100, 200, 100, 500],
    requireInteraction: true,
    tag: datos.tag || 'alarma-s360',
    renotify: true,
    data: { url: '/portal/dashboard.html' }
  };

  event.waitUntil(self.registration.showNotification(titulo, opciones));
});

// Al tocar la notificacion, abrir el panel (o traerlo al frente)
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
