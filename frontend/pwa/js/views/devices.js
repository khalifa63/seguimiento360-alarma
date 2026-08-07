import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

export async function renderDevices(container) {
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando sitios y dispositivos...</p></div>';

  try {
    const [sitesRes, devicesRes] = await Promise.all([
      Api.request('/api/sites', { auth: true }),
      Api.request('/api/devices', { auth: true })
    ]);

    const sites = sitesRes.payload || [];
    const devices = devicesRes.payload || [];

    // Agrupar dispositivos por site_id
    const devicesBySite = {};
    devices.forEach(d => {
      const siteId = d.site_id || 'unassigned';
      if (!devicesBySite[siteId]) devicesBySite[siteId] = [];
      devicesBySite[siteId].push(d);
    });

    let html = '<h2 style="margin-bottom:1rem;">Mis Sitios y Dispositivos</h2>';

    if (sites.length === 0) {
      html += '<div class="card"><p style="color:var(--text-secondary);">No tienes sitios registrados aún.</p></div>';
    } else {
      sites.forEach(site => {
        const siteDevices = devicesBySite[site.id] || [];
        html += `
          <div class="card">
            <h3 style="margin-bottom:0.5rem; color:var(--accent);">📍 ${escapeHtml(site.name)}</h3>
            <p style="color:var(--text-secondary); font-size:0.9rem; margin-bottom:1rem;">
              ${escapeHtml(site.address || 'Sin dirección')}
            </p>
            ${siteDevices.length > 0 ? `
              <h4 style="font-size:0.95rem; margin-bottom:0.5rem;">Dispositivos (${siteDevices.length}):</h4>
              <ul style="list-style:none; padding:0;">
                ${siteDevices.map(d => `
                  <li style="padding:0.5rem; border-left:3px solid var(--accent); margin-bottom:0.5rem; background:rgba(59,130,246,0.1); border-radius:4px;">
                    <strong>${escapeHtml(d.device_type)}</strong><br>
                    <span style="font-size:0.85rem; color:var(--text-secondary);">
                      ${escapeHtml(d.serial_number)} | Estado: ${escapeHtml(d.status)}
                    </span>
                  </li>
                `).join('')}
              </ul>
            ` : '<p style="color:var(--text-secondary); font-size:0.9rem;">Sin dispositivos asignados.</p>'}
          </div>
        `;
      });
    }

    // Dispositivos sin sitio asignado
    const unassigned = devicesBySite['unassigned'] || [];
    if (unassigned.length > 0) {
      html += `
        <div class="card">
          <h3 style="margin-bottom:0.5rem; color:var(--text-secondary);">⚠️ Dispositivos sin sitio (${unassigned.length})</h3>
          <ul style="list-style:none; padding:0;">
            ${unassigned.map(d => `
              <li style="padding:0.5rem; border-left:3px solid var(--text-secondary); margin-bottom:0.5rem;">
                <strong>${escapeHtml(d.device_type)}</strong><br>
                <span style="font-size:0.85rem; color:var(--text-secondary);">
                  ${escapeHtml(d.serial_number)} | Estado: ${escapeHtml(d.status)}
                </span>
              </li>
            `).join('')}
          </ul>
        </div>
      `;
    }

    html += `
      <div class="card" style="margin-top:2rem; text-align:center;">
        <h3 style="margin-bottom:1rem;">Panel de Control de Alarma</h3>
        <p style="color:var(--text-secondary); font-size:0.9rem; margin-bottom:1rem;">
          El armado/desarmado remoto y eventos en tiempo real estarán disponibles en la Fase 3.2
        </p>
        <button class="btn btn-secondary" disabled style="opacity:0.5; cursor:not-allowed;">
          🔒 Ir a Panel de Alarma (próximamente)
        </button>
      </div>
      <button class="btn" onclick="location.hash='#/dashboard'" style="margin-top:1rem;">
        ← Volver al Dashboard
      </button>
    `;

    container.innerHTML = html;
  } catch (err) {
    container.innerHTML = `
      <div class="alert alert-error">Error: ${escapeHtml(err.message)}</div>
      <button class="btn" onclick="location.hash='#/dashboard'">← Volver al Dashboard</button>
    `;
  }
}
