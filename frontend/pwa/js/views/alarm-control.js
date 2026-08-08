import { apiCall } from '../api.js?v=202608072018';
import { isAuthenticated, getUser } from '../auth.js?v=202608072018';
import { showNotification } from '../utils.js?v=202608072018';

export async function renderAlarmControl(container) {
  if (!isAuthenticated()) {
    window.location.hash = '#/login';
    return;
  }

  const user = getUser();

  container.innerHTML = `
    <div style="max-width: 800px; margin: 0 auto; padding: 20px;">
      <h1 style="text-align: center; margin-bottom: 30px;">🚨 Control de Alarma</h1>
      
      <div style="margin-bottom: 20px;">
        <label style="display: block; margin-bottom: 8px; font-weight: 600;">Seleccionar Sitio:</label>
        <select id="site-selector" style="width: 100%; padding: 12px; font-size: 16px; border: 2px solid #e2e8f0; border-radius: 8px; background: white; cursor: pointer;">
          <option value="">Cargando sitios...</option>
        </select>
      </div>

      <div id="alarm-panel" style="display: none; margin-top: 30px;">
        <div style="background: #f7fafc; padding: 20px; border-radius: 12px; margin-bottom: 20px;">
          <h3 style="margin: 0 0 10px 0; font-size: 18px;">Estado Actual</h3>
          <div id="alarm-status" style="font-size: 24px; font-weight: bold; text-align: center; padding: 20px;">
            Desconocido
          </div>
          <div id="hub-info" style="font-size: 14px; color: #718096; margin-top: 10px;"></div>
        </div>

        <div style="display: flex; gap: 15px; margin-bottom: 20px;">
          <button id="btn-arm" style="flex: 1; padding: 20px; font-size: 18px; font-weight: bold; background: #e53e3e; color: white; border: none; border-radius: 12px; cursor: pointer; transition: all 0.2s;">
            🔴 ARMAR
          </button>
          <button id="btn-disarm" style="flex: 1; padding: 20px; font-size: 18px; font-weight: bold; background: #38a169; color: white; border: none; border-radius: 12px; cursor: pointer; transition: all 0.2s;">
            🟢 DESARMAR
          </button>
        </div>

        <div id="alarm-message" style="padding: 15px; border-radius: 8px; margin-top: 20px; display: none;"></div>
      </div>

      <div id="no-hub-warning" style="display: none; background: #fff3cd; border: 2px solid #ffc107; padding: 20px; border-radius: 12px; margin-top: 20px;">
        <h3 style="margin: 0 0 10px 0; color: #856404;">⚠️ Sin Hubs Activos</h3>
        <p style="margin: 0; color: #856404;">
          Este sitio no tiene hubs activos. Por favor, registra y activa un hub desde la sección 
          <a href="#/devices" style="color: #007bff; font-weight: bold;">Dispositivos</a>.
        </p>
      </div>
    </div>
  `;

  const siteSelector = container.querySelector('#site-selector');
  const alarmPanel = container.querySelector('#alarm-panel');
  const noHubWarning = container.querySelector('#no-hub-warning');
  const alarmStatus = container.querySelector('#alarm-status');
  const hubInfo = container.querySelector('#hub-info');
  const btnArm = container.querySelector('#btn-arm');
  const btnDisarm = container.querySelector('#btn-disarm');
  const alarmMessage = container.querySelector('#alarm-message');

  // Cargar sitios
  try {
    const sites = await apiCall('/api/sites', 'GET');
    siteSelector.innerHTML = sites.length > 0 
      ? '<option value="">Selecciona un sitio</option>' + sites.map(s => `<option value="${s.id}">${s.name}</option>`).join('')
      : '<option value="">No hay sitios registrados</option>';
  } catch (error) {
    showNotification('Error al cargar sitios: ' + error.message, 'error');
    siteSelector.innerHTML = '<option value="">Error al cargar</option>';
  }

  let currentHubId = null;
  let pollingInterval = null;

  // Polling del estado cada 10 segundos
  async function pollAlarmState(siteId) {
    try {
      const state = await apiCall(`/api/alarm/state?site_id=${siteId}`, 'GET');
      
      if (state.state === 'armed') {
        alarmStatus.textContent = '🔴 ARMADO';
        alarmStatus.style.color = '#e53e3e';
        btnArm.disabled = true;
        btnDisarm.disabled = false;
        btnArm.style.opacity = '0.5';
        btnDisarm.style.opacity = '1';
      } else {
        alarmStatus.textContent = '🟢 DESARMADO';
        alarmStatus.style.color = '#38a169';
        btnArm.disabled = false;
        btnDisarm.disabled = true;
        btnArm.style.opacity = '1';
        btnDisarm.style.opacity = '0.5';
      }

      if (state.hub_name) {
        hubInfo.textContent = `Hub: ${state.hub_name} (${state.hub_status})`;
      }
    } catch (error) {
      console.error('Error en polling:', error);
    }
  }

  // Manejar selección de sitio
  siteSelector.addEventListener('change', async (e) => {
    const siteId = e.target.value;
    
    // Limpiar polling anterior
    if (pollingInterval) {
      clearInterval(pollingInterval);
      pollingInterval = null;
    }

    if (!siteId) {
      alarmPanel.style.display = 'none';
      noHubWarning.style.display = 'none';
      return;
    }

    try {
      // Obtener hubs del sitio
      const hubs = await apiCall(`/api/hubs?site_id=${siteId}`, 'GET');
      const activeHubs = hubs.filter(h => h.status === 'active');

      if (activeHubs.length === 0) {
        alarmPanel.style.display = 'none';
        noHubWarning.style.display = 'block';
        return;
      }

      // Usar el primer hub activo
      currentHubId = activeHubs[0].id;
      
      alarmPanel.style.display = 'block';
      noHubWarning.style.display = 'none';
      alarmMessage.style.display = 'none';

      // Polling inmediato y luego cada 10 segundos
      await pollAlarmState(siteId);
      pollingInterval = setInterval(() => pollAlarmState(siteId), 10000);

    } catch (error) {
      showNotification('Error al cargar hubs: ' + error.message, 'error');
    }
  });

  // Manejar armado
  btnArm.addEventListener('click', async () => {
    if (!currentHubId) return;

    btnArm.disabled = true;
    btnArm.textContent = '⏳ Armando...';

    try {
      const response = await apiCall('/api/alarm/control', 'POST', {
        action: 'arm',
        hub_id: currentHubId
      });

      showNotification(response.message + ' - El dispositivo lo ejecutará en su próximo ciclo de despertar (5-10 min)', 'success');
      
      // Actualizar estado inmediatamente
      const siteId = siteSelector.value;
      if (siteId) {
        await pollAlarmState(siteId);
      }
    } catch (error) {
      showNotification('Error al armar: ' + error.message, 'error');
    } finally {
      btnArm.disabled = false;
      btnArm.textContent = '🔴 ARMAR';
    }
  });

  // Manejar desarmado
  btnDisarm.addEventListener('click', async () => {
    if (!currentHubId) return;

    btnDisarm.disabled = true;
    btnDisarm.textContent = '⏳ Desarmando...';

    try {
      const response = await apiCall('/api/alarm/control', 'POST', {
        action: 'disarm',
        hub_id: currentHubId
      });

      showNotification(response.message + ' - El dispositivo lo ejecutará en su próximo ciclo de despertar (5-10 min)', 'success');
      
      // Actualizar estado inmediatamente
      const siteId = siteSelector.value;
      if (siteId) {
        await pollAlarmState(siteId);
      }
    } catch (error) {
      showNotification('Error al desarmar: ' + error.message, 'error');
    } finally {
      btnDisarm.disabled = false;
      btnDisarm.textContent = '🟢 DESARMAR';
    }
  });

  // Limpiar polling al salir
  window.addEventListener('hashchange', () => {
    if (pollingInterval) {
      clearInterval(pollingInterval);
    }
  }, { once: true });
}
