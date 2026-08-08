import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

let sites = [];
let selectedSiteId = null;
let pollingInterval = null;

function stopPolling() {
  if (pollingInterval) {
    clearInterval(pollingInterval);
    pollingInterval = null;
  }
}

function startPolling(container) {
  stopPolling();
  pollingInterval = setInterval(async () => {
    if (selectedSiteId) {
      await refreshState(container);
    }
  }, 10000); // Polling cada 10s (respetando batería)
}

async function refreshState(container) {
  if (!selectedSiteId) return;
  try {
    const state = await Api.alarmState(selectedSiteId);
    updateStateUI(state);
  } catch (err) {
    console.error('Error refrescando estado:', err);
  }
}

function updateStateUI(state) {
  const stateEl = document.getElementById('current-state');
  const armBtn = document.getElementById('btn-arm');
  const disarmBtn = document.getElementById('btn-disarm');
  if (!stateEl || !armBtn || !disarmBtn) return;

  const isArmed = state.state === 'armed';
  stateEl.textContent = isArmed ? '🔴 ARMADA' : '🟢 DESARMADA';
  stateEl.style.color = isArmed ? 'var(--error, #ef4444)' : 'var(--success, #10b981)';
  stateEl.style.fontWeight = 'bold';
  stateEl.style.fontSize = '1.5rem';

  armBtn.disabled = isArmed;
  disarmBtn.disabled = !isArmed;
  armBtn.style.opacity = isArmed ? '0.4' : '1';
  disarmBtn.style.opacity = !isArmed ? '0.4' : '1';

  const metaEl = document.getElementById('state-meta');
  if (metaEl && state.updated_at) {
    metaEl.textContent = 'Última actualización: ' + new Date(state.updated_at).toLocaleString();
  }
}

async function sendCommand(action) {
  const msg = document.getElementById('alarm-msg');
  const hubs = await Api.hubs(selectedSiteId);
  const activeHub = hubs.find(h => h.status === 'active');
  if (!activeHub) {
    msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ No hay hubs activos en este sitio</p>';
    return;
  }

  try {
    msg.innerHTML = '<p style="color:var(--accent);">⏳ Enviando comando...</p>';
    const res = await Api.alarmControl({ action, hub_id: activeHub.id });
    msg.innerHTML = '<p style="color:var(--success, #10b981);">✅ ' + escapeHtml(res.message) + '</p>' +
      '<p style="color:var(--text-secondary); font-size:0.85rem;">💡 ' + escapeHtml(res.note) + '</p>';
    setTimeout(() => refreshState(document.getElementById('alarm-container')), 500);
    setTimeout(() => { msg.innerHTML = ''; }, 8000);
  } catch (err) {
    msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ ' + escapeHtml(err.message) + '</p>';
  }
}

export async function renderAlarmControl(container) {
  container.id = 'alarm-container';
  stopPolling();

  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando...</p></div>';

  try {
    sites = await Api.sites();
  } catch (err) {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
    return;
  }

  let html = '<h2 style="margin-bottom:1rem;">🚨 Control de Alarma</h2>';

  // Selector de sitio
  html += '<div class="card" style="margin-bottom:1rem;">';
  html += '<label style="display:block; margin-bottom:0.5rem; font-weight:bold;">Sitio:</label>';
  html += '<select id="site-selector" style="width:100%; padding:0.75rem; font-size:1rem;">';
  html += '<option value="">-- Selecciona un sitio --</option>';
  sites.forEach(s => {
    html += '<option value="' + s.id + '">' + escapeHtml(s.name) + '</option>';
  });
  html += '</select>';
  html += '</div>';

  // Panel de control (se muestra al seleccionar sitio)
  html += '<div id="control-panel" style="display:none;">';

  // Estado actual
  html += '<div class="card" style="margin-bottom:1rem; text-align:center;">';
  html += '<h3 style="margin-bottom:0.5rem;">Estado actual</h3>';
  html += '<div id="current-state" style="font-size:1.5rem; margin:1rem 0;">🟢 DESARMADA</div>';
  html += '<p id="state-meta" style="color:var(--text-secondary); font-size:0.85rem;"></p>';
  html += '</div>';

  // Botones de control
  html += '<div style="display:grid; grid-template-columns:1fr 1fr; gap:0.75rem; margin-bottom:1rem;">';
  html += '<button id="btn-arm" class="btn" style="padding:1.5rem; font-size:1.1rem; background:var(--error, #ef4444); color:white; font-weight:bold;">🔴 ARMAR</button>';
  html += '<button id="btn-disarm" class="btn" style="padding:1.5rem; font-size:1.1rem; background:var(--success, #10b981); color:white; font-weight:bold;">🟢 DESARMAR</button>';
  html += '</div>';

  // Mensaje de resultado
  html += '<div id="alarm-msg" style="margin-bottom:1rem;"></div>';

  // Info del hub
  html += '<div id="hub-info" class="card" style="font-size:0.85rem; color:var(--text-secondary);"></div>';

  html += '</div>';

  html += '<button class="btn" onclick="location.hash=\'#/dashboard\'" style="margin-top:1.5rem;">← Volver al Dashboard</button>';

  container.innerHTML = html;

  // Event handlers
  document.getElementById('site-selector').addEventListener('change', async (e) => {
    selectedSiteId = parseInt(e.target.value, 10);
    const panel = document.getElementById('control-panel');
    if (!selectedSiteId) {
      panel.style.display = 'none';
      stopPolling();
      return;
    }
    panel.style.display = 'block';

    // Cargar hubs y estado
    try {
      const [hubs, state] = await Promise.all([
        Api.hubs(selectedSiteId),
        Api.alarmState(selectedSiteId)
      ]);

      const hubInfo = document.getElementById('hub-info');
      const activeHub = hubs.find(h => h.status === 'active');
      if (activeHub) {
        hubInfo.innerHTML = '<strong>Hub activo:</strong> ' + escapeHtml(activeHub.name) +
          '<br><strong>Serial:</strong> ' + escapeHtml(activeHub.serial_number) +
          '<br><strong>Último visto:</strong> ' + (activeHub.last_seen ? new Date(activeHub.last_seen).toLocaleString() : 'nunca');
      } else {
        hubInfo.innerHTML = '⚠️ No hay hubs activos en este sitio. Registra uno desde <a href="#/devices" style="color:var(--accent);">Dispositivos</a>.';
      }

      updateStateUI(state);
      startPolling(container);
    } catch (err) {
      alert('Error cargando estado: ' + err.message);
    }
  });

  document.getElementById('btn-arm').addEventListener('click', () => sendCommand('arm'));
  document.getElementById('btn-disarm').addEventListener('click', () => sendCommand('disarm'));
}

// Limpiar polling al salir de la vista
window.addEventListener('hashchange', () => {
  if (!location.hash.includes('alarm-control')) {
    stopPolling();
  }
});
