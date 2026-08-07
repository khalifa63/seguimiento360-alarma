import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml, formatDate } from '../utils.js?v=__V__';

export function renderDashboard(container) {
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando perfil...</p></div>';

  Api.profile().then((data) => {
    const u = data.user;
    const exp = Auth.tokenExpiry();
    const expTxt = exp ? new Date(exp).toLocaleString('es-MX') : '—';

    container.innerHTML = `
      <div class="card">
        <h2 style="margin-bottom:0.25rem;">Hola, ${escapeHtml(u.full_name || 'cliente')}</h2>
        <p style="color:var(--text-secondary); font-size:0.9rem;">${escapeHtml(u.email)}</p>
      </div>

      <div class="card">
        <h3 style="margin-bottom:1rem; font-size:1rem; color:var(--accent);">Datos de la cuenta</h3>
        <p style="margin-bottom:0.5rem;"><strong>Inquilino:</strong> ${escapeHtml(u.tenant_name || '—')}</p>
        <p style="margin-bottom:0.5rem;"><strong>Rol:</strong> ${escapeHtml(u.role)}</p>
        <p style="margin-bottom:0.5rem;"><strong>Teléfono:</strong> ${escapeHtml(u.phone || '—')}</p>
        <p style="margin-bottom:0.5rem;"><strong>Cliente desde:</strong> ${formatDate(u.created_at)}</p>
        <p style="margin-bottom:0.5rem;"><strong>Estado:</strong> ${u.is_active ? 'Activa' : 'Inactiva'}</p>
        <p style="color:var(--text-secondary); font-size:0.8rem;">Sesión expira: ${escapeHtml(expTxt)}</p>
      </div>

      <div class="card">
        <h3 style="margin-bottom:0.5rem; font-size:1rem; color:var(--accent);">Panel de Alarma</h3>
        <p style="color:var(--text-secondary); font-size:0.9rem;">
          El armado/desarmado y los eventos en tiempo real llegan en la Fase 3.
        </p>
      </div>

      <button class="btn" onclick="location.hash='#/devices'" style="margin-bottom:1rem;">
        📋 Ver Mis Sitios y Dispositivos
      </button>
      <button class="btn" onclick="location.hash='#/devices'" style="margin-bottom:1rem;">
        📋 Ver Mis Sitios y Dispositivos
      </button>
      <button class="btn btn-secondary" id="logout-btn">Cerrar Sesión</button>
    `;
    document.getElementById('logout-btn').addEventListener('click', () => Auth.logout());
  }).catch((err) => {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
  });
}
