import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';
import { renderAlarmControl } from './alarm-control.js?v=__V__';

export async function renderDashboard(container) {
  const user = Auth.getUser();
  const userName = user ? (user.full_name || user.email) : 'Usuario';

  let html = '<div class="card">';
  html += '<h2 style="margin-bottom:0.5rem;">Bienvenido, ' + escapeHtml(userName) + '</h2>';
  html += '<p style="color:var(--text-secondary);">Rol: ' + escapeHtml(user && user.role ? user.role : 'customer') + '</p>';
  html += '</div>';

  html += '<div style="display:grid; gap:1rem; margin-top:1.5rem;">';

  // Botón alarma: renderiza la vista DIRECTAMENTE (bypass del router)
  html += '<a href="#" id="alarm-link" class="card" style="text-decoration:none; color:inherit; display:block; border-left:4px solid var(--error, #ef4444);">';
  html += '<h3 style="margin:0 0 0.25rem 0; color:var(--text-primary, #fff);">🚨 Control de Alarma</h3>';
  html += '<p style="margin:0; color:var(--text-secondary);">Armar o desarmar tus sitios</p>';
  html += '</a>';

  html += '<a href="#/devices" class="card" style="text-decoration:none; color:inherit; display:block;">';
  html += '<h3 style="margin:0 0 0.25rem 0; color:var(--text-primary, #fff);">📋 Sitios y Dispositivos</h3>';
  html += '<p style="margin:0; color:var(--text-secondary);">Gestionar tus sitios, hubs y dispositivos</p>';
  html += '</a>';

  html += '</div>';

  html += '<button class="btn btn-secondary" id="logout-btn" style="margin-top:2rem;">Cerrar sesión</button>';

  container.innerHTML = html;

  // Bypass del router: renderiza la vista de alarma en el mismo contenedor
  document.getElementById('alarm-link').addEventListener('click', function(e) {
    e.preventDefault();
    renderAlarmControl(container);
  });

  document.getElementById('logout-btn').addEventListener('click', function() {
    Auth.logout();
  });
}
