import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

async function loadSites(container) {
  try {
    const sites = await Api.sites();
    renderList(container, sites);
  } catch (err) {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
  }
}

function renderList(container, sites) {
  let html = '<h2 style="margin-bottom:1rem;">Mis Sitios</h2>';

  html += '<div class="card" style="margin-bottom:1.5rem;">';
  html += '<h3 style="margin-bottom:0.75rem;">Crear nuevo sitio</h3>';
  html += '<form id="site-form">';
  html += '<input type="text" id="site-name" placeholder="Nombre del sitio *" required style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">';
  html += '<input type="text" id="site-address" placeholder="Dirección" style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">';
  html += '<button type="submit" class="btn" style="width:100%;">Crear sitio</button>';
  html += '</form>';
  html += '<div id="site-msg"></div>';
  html += '</div>';

  html += '<h3 style="margin-bottom:0.75rem;">Sitios registrados (' + sites.length + ')</h3>';
  if (sites.length === 0) {
    html += '<div class="card"><p style="color:var(--text-secondary);">Aún no tienes sitios. Crea el primero arriba.</p></div>';
  } else {
    sites.forEach(s => {
      html += '<div class="card" style="margin-bottom:0.75rem;">';
      html += '<h4 style="color:var(--accent); margin-bottom:0.5rem;">📍 ' + escapeHtml(s.name) + '</h4>';
      html += '<p style="color:var(--text-secondary); font-size:0.9rem; margin-bottom:0.25rem;">' + escapeHtml(s.address || 'Sin dirección') + '</p>';
      html += '<p style="color:var(--text-secondary); font-size:0.8rem;">ID: ' + s.id + ' | Creado: ' + new Date(s.created_at).toLocaleDateString() + '</p>';
      html += '<button class="btn btn-secondary" onclick="window._editSite(' + s.id + ')" style="margin-top:0.5rem; font-size:0.85rem;">Editar</button>';
      html += '</div>';
    });
  }

  html += '<button class="btn" onclick="location.hash=\'#/dashboard\'" style="margin-top:1.5rem;">← Volver al Dashboard</button>';
  container.innerHTML = html;

  document.getElementById('site-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const msg = document.getElementById('site-msg');
    const name = document.getElementById('site-name').value.trim();
    const address = document.getElementById('site-address').value.trim();
    if (!name) { msg.innerHTML = '<p style="color:red;">Nombre requerido</p>'; return; }
    try {
      await Api.createSite({ name, address });
      msg.innerHTML = '<p style="color:green;">✅ Sitio creado</p>';
      document.getElementById('site-form').reset();
      setTimeout(() => loadSites(container), 800);
    } catch (err) {
      msg.innerHTML = '<p style="color:red;">' + escapeHtml(err.message) + '</p>';
    }
  });

  window._editSite = async (id) => {
    const newName = prompt('Nuevo nombre:');
    if (!newName) return;
    try {
      await Api.updateSite(id, { name: newName });
      loadSites(container);
    } catch (err) {
      alert('Error: ' + err.message);
    }
  };
}

export async function renderDevices(container) {
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando sitios...</p></div>';
  await loadSites(container);
}
