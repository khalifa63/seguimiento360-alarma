import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

export async function renderDashboard(container) {
  const user = Auth.getUser();
  const userName = user ? (user.full_name || user.email) : 'Usuario';

  let html = '<div class="card">';
  html += '<h2 style="margin-bottom:0.5rem;">Bienvenido, ' + escapeHtml(userName) + '</h2>';
  html += '<p style="color:var(--text-secondary);">Rol: ' + escapeHtml(user?.role || 'customer') + '</p>';
  html += '</div>';

  html += '<div style="display:grid; gap:1rem; margin-top:1.5rem;">';

  // Botón de alarma (destacado)
  html += '<a href="#/alarm-control" class="card" style="text-decoration:none; border-left:4px solid var(--error, #ef4444); cursor:pointer;">';
  html += '<h3 style="margin:0 0 0.25rem 0;">🚨 Control de Alarma</h3>';
  html += '<p style="margin:0; color:var(--text-secondary);">Armar o desarmar tus sitios</p>';
  html += '</a>';

  // Botón de dispositivos
  html += '<a href="#/devices" class="card" style="text-decoration:none; cursor:pointer;">';
  html += '<h3 style="margin:0 0 0.25rem 0;">📋 Sitios y Dispositivos</h3>';
  html += '<p style="margin:0; color:var(--text-secondary);">Gestionar tus sitios, hubs y dispositivos</p>';
  html += '</a>';

  html += '</div>';

  html += '<button class="btn btn-secondary" onclick="location.hash=\'#/login\'" style="margin-top:2rem;" id="logout-btn">Cerrar sesión</button>';

  container.innerHTML = html;

  document.getElementById('logout-btn').addEventListener('click', () => {
    Auth.logout();
  });
}
