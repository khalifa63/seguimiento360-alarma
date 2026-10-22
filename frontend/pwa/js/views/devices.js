import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

let currentSites = [];
let currentDevices = [];
let currentUser = null;

function escAttr(v) {
  return String(v == null ? '' : v)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function cardHtml(s) {
  return '<div class="card" style="margin-bottom:0.75rem;" id="site-card-' + s.id + '">' +
    '<h4 style="color:var(--accent); margin-bottom:0.5rem;">📍 ' + escapeHtml(s.name) + '</h4>' +
    '<p style="color:var(--text-secondary); font-size:0.9rem; margin-bottom:0.25rem;">' + escapeHtml(s.address || 'Sin dirección') + '</p>' +
    '<p style="color:var(--text-secondary); font-size:0.8rem;">ID: ' + s.id + ' | Creado: ' + new Date(s.created_at).toLocaleDateString() + '</p>' +
    '<button class="btn btn-secondary" data-edit="' + s.id + '" style="margin-top:0.5rem; font-size:0.85rem;">✏️ Editar</button>' +
    '</div>';
}

function editHtml(s) {
  return '<div class="card" style="margin-bottom:0.75rem; border:1px solid var(--accent);" id="site-card-' + s.id + '">' +
    '<h4 style="margin-bottom:0.5rem;">✏️ Editar sitio #' + s.id + '</h4>' +
    '<input type="text" data-f="name" value="' + escAttr(s.name) + '" style="width:100%; margin-bottom:0.5rem; padding:0.5rem;" placeholder="Nombre *">' +
    '<input type="text" data-f="address" value="' + escAttr(s.address || '') + '" style="width:100%; margin-bottom:0.5rem; padding:0.5rem;" placeholder="Dirección">' +
    '<button class="btn" data-save="' + s.id + '" style="margin-right:0.5rem;">Guardar</button>' +
    '<button class="btn btn-secondary" data-cancel="' + s.id + '">Cancelar</button>' +
    '</div>';
}

function deviceCardHtml(d) {
  const statusColor = d.status === 'active' ? 'var(--success, #10b981)' : 'var(--text-secondary)';
  return '<div class="card" style="margin-bottom:0.5rem; padding:0.75rem;">' +
    '<div style="display:flex; justify-content:space-between; align-items:center;">' +
    '<div>' +
    '<strong style="color:var(--accent);">' + escapeHtml(d.device_type) + '</strong>' +
    '<div style="font-size:0.8rem; color:var(--text-secondary);">SN: ' + escapeHtml(d.serial_number) + '</div>' +
    '<div style="font-size:0.75rem; color:var(--text-secondary);">Vinculado: ' + new Date(d.bound_at).toLocaleString() + '</div>' +
    '</div>' +
    '<div style="display:flex; align-items:center; gap:0.25rem;">' +
    '<span style="width:8px; height:8px; border-radius:50%; background:' + statusColor + ';"></span>' +
    '<span style="font-size:0.8rem;">' + escapeHtml(d.status) + '</span>' +
    '</div>' +
    '</div>' +
    '</div>';
}

async function refresh(container) {
  try {
    const [sites, devices] = await Promise.all([Api.sites(), Api.devices()]);
    currentSites = sites;
    currentDevices = devices;
    render(container);
  } catch (err) {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
  }
}

function render(container) {
  const user = currentUser || Auth.getUser();
  const isAdmin = user && user.role === 'admin';

  let html = '<h2 style="margin-bottom:1rem;">Gestión de Sitios y Dispositivos</h2>';

  // === SECCIÓN 1: VINCULAR DISPOSITIVO ===
  html += '<div class="card" style="margin-bottom:1.5rem; border-left:4px solid var(--accent);">';
  html += '<h3 style="margin-bottom:0.75rem;">🔗 Vincular nuevo dispositivo</h3>';
  html += '<form id="claim-form">';
  html += '<input type="text" id="claim-code" placeholder="Código de vinculación (XX-YYYYYY-Z)" required ' +
    'style="width:100%; margin-bottom:0.5rem; padding:0.5rem; font-family:monospace; text-transform:uppercase;">';
  html += '<select id="claim-site" required style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">';
  html += '<option value="">-- Selecciona un sitio --</option>';
  currentSites.forEach(s => {
    html += '<option value="' + s.id + '">' + escapeHtml(s.name) + '</option>';
  });
  html += '</select>';
  html += '<button type="submit" class="btn" style="width:100%;">Vincular dispositivo</button>';
  html += '</form>';
  html += '<div id="claim-msg" style="margin-top:0.5rem;"></div>';
  if (currentSites.length === 0) {
    html += '<p style="color:var(--text-secondary); font-size:0.85rem; margin-top:0.5rem;">' +
      '💡 Primero crea un sitio abajo antes de vincular dispositivos.</p>';
  }
  html += '</div>';

  // === SECCIÓN 2: CREAR/EDITAR SITIOS ===
  html += '<div class="card" style="margin-bottom:1.5rem;">';
  html += '<h3 style="margin-bottom:0.75rem;">➕ Crear nuevo sitio</h3>';
  html += '<form id="site-form">';
  html += '<input type="text" id="site-name" placeholder="Nombre del sitio *" required style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">';
  html += '<input type="text" id="site-address" placeholder="Dirección" style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">';
  html += '<button type="submit" class="btn" style="width:100%;">Crear sitio</button>';
  html += '</form>';
  html += '<div id="site-msg" style="margin-top:0.5rem;"></div>';
  html += '</div>';

  // === SECCIÓN 3: LISTADO DE SITIOS CON SUS DISPOSITIVOS ===
  html += '<h3 style="margin-bottom:0.75rem;">📋 Mis Sitios (' + currentSites.length + ')</h3>';
  if (currentSites.length === 0) {
    html += '<div class="card"><p style="color:var(--text-secondary);">Aún no tienes sitios. Crea el primero arriba.</p></div>';
  } else {
    const devicesBySite = {};
    currentDevices.forEach(d => {
      if (!devicesBySite[d.site_id]) devicesBySite[d.site_id] = [];
      devicesBySite[d.site_id].push(d);
    });

    currentSites.forEach(s => {
      const siteDevices = devicesBySite[s.id] || [];
      html += '<div class="card" style="margin-bottom:1rem;" id="site-card-' + s.id + '">';
      html += '<h4 style="color:var(--accent); margin-bottom:0.5rem;">📍 ' + escapeHtml(s.name) + '</h4>';
      html += '<p style="color:var(--text-secondary); font-size:0.9rem; margin-bottom:0.25rem;">' + escapeHtml(s.address || 'Sin dirección') + '</p>';
      html += '<div style="display:flex; gap:0.5rem; margin-bottom:0.75rem;">';
      html += '<button class="btn btn-secondary" data-edit="' + s.id + '" style="font-size:0.85rem;">✏️ Editar</button>';
      html += '</div>';
      html += '<h5 style="font-size:0.9rem; margin-bottom:0.5rem; color:var(--text-secondary);">Dispositivos (' + siteDevices.length + ')</h5>';
      if (siteDevices.length === 0) {
        html += '<p style="color:var(--text-secondary); font-size:0.85rem; font-style:italic;">Sin dispositivos vinculados</p>';
      } else {
        siteDevices.forEach(d => { html += deviceCardHtml(d); });
      }
      html += '</div>';
    });
  }

  // === SECCIÓN 4: DISPOSITIVOS SIN SITIO ===
  const unassignedDevices = currentDevices.filter(d => !d.site_id);
  if (unassignedDevices.length > 0) {
    html += '<h3 style="margin-bottom:0.75rem; color:var(--text-secondary);">⚠️ Dispositivos sin sitio (' + unassignedDevices.length + ')</h3>';
    unassignedDevices.forEach(d => { html += deviceCardHtml(d); });
  }

  // === SECCIÓN 5: ADMIN (solo si role === 'admin') ===
  if (isAdmin) {
    html += '<div class="card" style="margin-top:1.5rem; border-left:4px solid var(--warning, #f59e0b);">';
    html += '<h3 style="margin-bottom:0.75rem; color:var(--warning, #f59e0b);">🛡️ Panel de Administración</h3>';
    html += '<h4 style="margin-bottom:0.5rem;">Generar códigos de vinculación</h4>';
    html += '<form id="gen-form">';
    html += '<div style="display:flex; gap:0.5rem; margin-bottom:0.5rem;">';
    html += '<input type="text" id="gen-prefix" placeholder="Prefijo (2 chars)" maxlength="2" value="S3" style="flex:1; padding:0.5rem; font-family:monospace; text-transform:uppercase;">';
    html += '<input type="number" id="gen-quantity" placeholder="Cantidad" min="1" max="100" value="5" style="flex:1; padding:0.5rem;">';
    html += '</div>';
    html += '<button type="submit" class="btn" style="width:100%;">Generar códigos</button>';
    html += '</form>';
    html += '<div id="gen-msg" style="margin-top:0.5rem;"></div>';
    html += '</div>';
  }

  html += '<button class="btn" onclick="location.hash=\'#/dashboard\'" style="margin-top:1.5rem;">← Volver al Dashboard</button>';
  container.innerHTML = html;

  // === EVENT HANDLERS ===

  // Formulario de vinculación
  document.getElementById('claim-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const msg = document.getElementById('claim-msg');
    const code = document.getElementById('claim-code').value.trim().toUpperCase();
    const siteId = parseInt(document.getElementById('claim-site').value, 10);
    if (!code || !siteId) {
      msg.innerHTML = '<p style="color:var(--error, #ef4444);">Completa todos los campos</p>';
      return;
    }
    try {
      msg.innerHTML = '<p style="color:var(--accent);">⏳ Vinculando...</p>';
      const res = await Api.claimDevice({ code, site_id: siteId });
      msg.innerHTML = '<p style="color:var(--success, #10b981);">✅ ' + escapeHtml(res.message) + '</p>';
      document.getElementById('claim-form').reset();
      setTimeout(() => refresh(container), 1000);
    } catch (err) {
      msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ ' + escapeHtml(err.message) + '</p>';
    }
  });

  // Formulario de crear sitio
  document.getElementById('site-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const msg = document.getElementById('site-msg');
    const name = document.getElementById('site-name').value.trim();
    const address = document.getElementById('site-address').value.trim();
    if (!name) { msg.innerHTML = '<p style="color:var(--error, #ef4444);">Nombre requerido</p>'; return; }
    try {
      await Api.createSite({ name, address });
      msg.innerHTML = '<p style="color:var(--success, #10b981);">✅ Sitio creado</p>';
      document.getElementById('site-form').reset();
      setTimeout(() => refresh(container), 800);
    } catch (err) {
      msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ ' + escapeHtml(err.message) + '</p>';
    }
  });

  // Delegación de eventos para editar sitios
  container.onclick = async (e) => {
    const t = e.target;
    if (!t.getAttribute) return;
    const editId = t.getAttribute('data-edit');
    const saveId = t.getAttribute('data-save');
    const cancelId = t.getAttribute('data-cancel');

    if (editId) {
      const s = currentSites.find(x => x.id === Number(editId));
      const card = document.getElementById('site-card-' + editId);
      if (s && card) card.outerHTML = editHtml(s);
    } else if (cancelId) {
      render(container);
    } else if (saveId) {
      const card = document.getElementById('site-card-' + saveId);
      const name = card.querySelector('[data-f="name"]').value.trim();
      const address = card.querySelector('[data-f="address"]').value.trim();
      if (!name) { alert('El nombre es requerido'); return; }
      try {
        await Api.updateSite(Number(saveId), { name, address });
        await refresh(container);
      } catch (err) {
        alert('Error: ' + err.message);
      }
    }
  };

  // Formulario de generación de códigos (solo admin)
  const genForm = document.getElementById('gen-form');
  if (genForm) {
    genForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      const msg = document.getElementById('gen-msg');
      const prefix = document.getElementById('gen-prefix').value.trim().toUpperCase();
      const quantity = parseInt(document.getElementById('gen-quantity').value, 10);
      if (!prefix || prefix.length !== 2) {
        msg.innerHTML = '<p style="color:var(--error, #ef4444);">Prefijo debe ser 2 caracteres</p>';
        return;
      }
      if (!quantity || quantity < 1 || quantity > 100) {
        msg.innerHTML = '<p style="color:var(--error, #ef4444);">Cantidad entre 1 y 100</p>';
        return;
      }
      try {
        msg.innerHTML = '<p style="color:var(--accent);">⏳ Generando...</p>';
        const res = await Api.generateCodes({ prefix, quantity });
        let codesHtml = '<p style="color:var(--success, #10b981); margin-bottom:0.5rem;">✅ Generados ' + res.generated + ' códigos:</p>';
        codesHtml += '<div style="background:rgba(0,0,0,0.2); padding:0.5rem; border-radius:4px; font-family:monospace; font-size:0.85rem;">';
        res.codes.forEach(c => {
          codesHtml += '<div style="padding:2px 0;">' + escapeHtml(c) + '</div>';
        });
        codesHtml += '</div>';
        msg.innerHTML = codesHtml;
      } catch (err) {
        msg.innerHTML = '<p style="color:var(--error, #ef4444);">❌ ' + escapeHtml(err.message) + '</p>';
      }
    });
  }
}

export async function renderDevices(container) {
  currentUser = Auth.getUser();
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando...</p></div>';
  await refresh(container);
}
