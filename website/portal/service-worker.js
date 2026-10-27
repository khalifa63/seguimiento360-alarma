// Service Worker con vibración táctica e interacción obligatoria
self.addEventListener('push', (event) => {
  const data = event.data ? event.data.text() : 'Alarma Seguimiento 360';
  
  const options = {
    body: data,
    icon: '/portal/icono.png',
    // Patrón de vibración: suena como una alerta seria
    vibrate: [200, 100, 200, 100, 200, 100, 200, 100, 500],
    // La notificación no desaparece sola, requiere acción del usuario
    requireInteraction: true,
    // Prioridad alta para que se muestre arriba
    priority: 'high'
  };

  event.waitUntil(
    self.registration.showNotification('Seguimiento 360', options)
  );
});