import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

let currentSites = [];
let selectedSiteId = null;
let pollingInterval = null;

function stopPolling() {
  if (pollingInterval) {
    clearInterval(pollingInterval);
    pollingInterval = null;
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
  stateEl.style.fontSize = '1.4rem';

  armBtn.disabled = isArmed;
  disarmBtn.disabled = !isArmed;
  armBtn.style.opacity = isArmed ? '0.4' : '1';
  disarmBtn.style.opacity = !isArmed ? '0.4' : '1';

  const metaEl = document.getElementById('state-meta');
  if (metaEl && state.updated_at) {
    metaEl.textContent = 'Actualizado: ' + new Date(state.updated_at).toLocaleString();
  }
}

async function refreshState() {
  if (!selectedSiteId) return;
  try {
    const state = await Api.alarmState(selectedSiteId);
    updateStateUI(state);
  } catch (err) {
    console.error('polling estado:', err);
  }
}

export async function renderAlarmControl(container) {
  stopPolling();
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando...</p></div>';

  try {
    currentSites = await Api.sites();
  } catch (err) {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
    return;
  }

  let html = '<h2 style="margin-bottom:1rem;">🚨 Control de Alarma</h2>';

  html += '<div class="card" style="margin-bottom:1rem;">';
  html += '<label style="display:block; margin-bottom:0.5rem; font-weight:bold;">Sitio:</label>';
  html += '<select id="site-selector" style="width:100%; padding:0.75rem; font-size:1rem;">';
  html += '<option value="">-- Selecciona un sitio --</option>';
  currentSites.forEach(function(s) {
    html += '<option value="' + s.id + '">' + escapeHtml(s.name) + '</option>';
  });
  html += '</select>';
  html += '</div>';

  html += '<div id="control-panel" style="display:none;">';

  html += '<div class="card" style="margin-bottom:1rem; text-align:center;">';
  html += '<h3 style="margin-bottom:0.5rem;">Estado actual</h3>';
  html += '<div id="current-state" style="margin:0.75rem 0;">🟢 DESARMADA</div>';
  html += '<p id="state-meta" style="color:var(--text-secondary); font-size:0.85rem;"></p>';
  html += '</div>';

  html += '<div style="display:grid; grid-template-columns:1fr 1fr; gap:0.75rem; margin-bottom:1rem;">';
  html += '<button id="btn-arm" class="btn" style="padding:1.25rem; font-size:1.05rem; background:var(--error, #ef4444); color:#fff; font-weight:bold;">🔴 ARMAR</button>';
  html += '<button id="btn-disarm" class="btn" style="padding:1.25rem; font-size:1.05rem; background:var(--success, #10b981); color:#fff; font-weight:bold;">🟢 DESARMAR</button>';
  html += '</div>';

  html += '<div id="alarm-msg" style="margin-bottom:1rem;"></div>';

  html += '<div id="hub-info" class="card" style="font-size:0.85rem; color:var(--text-secondary);"></div>';

  html += '</div>';

  html += '<button class="btn" onclick="location.hash=\'#/dashboard\'" style="margin-top:1.5rem;">← Volver al Dashboard</button>';

  container.innerHTML = html;

  document.getElementById('site-selector').addEventListener('change', async function(e) {
    selectedSiteId = parseInt(e.target.value, 10);
    const panel = document.getElementById('control-panel');
    if (!selectedSiteId) {
      panel.style.display = 'none';
      stopPolling();
      return;
    }
    panel.style.display = 'block';

    try {
      const results = await Promise.all([
        Api.hubs(selectedSiteId),
        Api.alarmState(selectedSiteId)
      ]);
      const hubs = results[0];
      const state = results[1];

      const hubInfo = document.getElementById('hub-info');
      const activeHub = hubs.find(function(h) { return h.status === 'active'; });
      if (activeHub) {
        hubInfo.innerHTML = '<strong>Hub activo:</strong> ' + escapeHtml(activeHub.name) +
          '<br><strong>Serial:</strong> ' + escapeHtml(activeHub.serial_number) +
          '<br><strong>Último visto:</strong> ' + (activeHub.last_seen ? new Date(activeHub.last_seen).toLocaleString() : 'nunca');
      } else {
        hubInfo.innerHTML = '⚠️ No hay hubs activos en este sitio. Registra uno en <a href="#/devices" style="color:var(--accent);">Dispositivos</a>.';
      }

      updateStateUI(state);
      stopPolling();
      pollingInterval = setInterval(refreshState, 10000);
    } catch (err) {
      alert('Error cargando estado: ' + err.message);
    }
  });

  document.getElementById('btn-arm').addEventListener('click', function() { sendCommand('arm'); });
  document.getElementById('btn-disarm').addEventListener('click', function() { sendCommand('disarm'); });

  async function sendCommand(action) {
    const msg = document.getElementById('alarm-msg');
    try {
      const hubs = await Api.hubs(selectedSiteId);
      const activeHub = hubs.find(function(h) { return h.status === 'active'; });
      if (!activeHub) {
        msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ No hay hubs activos en este sitio</p>';
        return;
      }
      msg.innerHTML = '<p style="color:var(--accent);">⏳ Enviando comando...</p>';
      const res = await Api.alarmControl({ action: action, hub_id: activeHub.id });
      msg.innerHTML = '<p style="color:var(--success, #10b981);">✅ ' + escapeHtml(res.message) + '</p>' +
        '<p style="color:var(--text-secondary); font-size:0.85rem;">💡 ' + escapeHtml(res.note) + '</p>';
      setTimeout(refreshState, 500);
      setTimeout(function() { msg.innerHTML = ''; }, 8000);
    } catch (err) {
      msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ ' + escapeHtml(err.message) + '</p>';
    }
  }
}

window.addEventListener('hashchange', function() {
  if (!location.hash.includes('alarm-control')) {
    stopPolling();
  }
});
