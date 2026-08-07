import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

let currentSites = [];

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

function renderList(container) {
  let html = '<h2 style="margin-bottom:1rem;">Mis Sitios</h2>';

  html += '<div class="card" style="margin-bottom:1.5rem;">' +
    '<h3 style="margin-bottom:0.75rem;">Crear nuevo sitio</h3>' +
    '<form id="site-form">' +
    '<input type="text" id="site-name" placeholder="Nombre del sitio *" required style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">' +
    '<input type="text" id="site-address" placeholder="Dirección" style="width:100%; margin-bottom:0.5rem; padding:0.5rem;">' +
    '<button type="submit" class="btn" style="width:100%;">Crear sitio</button>' +
    '</form><div id="site-msg"></div></div>';

  html += '<h3 style="margin-bottom:0.75rem;">Sitios registrados (' + currentSites.length + ')</h3>';
  if (currentSites.length === 0) {
    html += '<div class="card"><p style="color:var(--text-secondary);">Aún no tienes sitios. Crea el primero arriba.</p></div>';
  } else {
    currentSites.forEach(s => { html += cardHtml(s); });
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
      setTimeout(() => loadSites(container), 600);
    } catch (err) {
      msg.innerHTML = '<p style="color:red;">' + escapeHtml(err.message) + '</p>';
    }
  });

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
      const s = currentSites.find(x => x.id === Number(cancelId));
      const card = document.getElementById('site-card-' + cancelId);
      if (s && card) card.outerHTML = cardHtml(s);
    } else if (saveId) {
      const card = document.getElementById('site-card-' + saveId);
      const name = card.querySelector('[data-f="name"]').value.trim();
      const address = card.querySelector('[data-f="address"]').value.trim();
      if (!name) { alert('El nombre es requerido'); return; }
      try {
        await Api.updateSite(Number(saveId), { name: name, address: address });
        await loadSites(container);
      } catch (err) {
        alert('Error: ' + err.message);
      }
    }
  };
}

async function loadSites(container) {
  try {
    currentSites = await Api.sites();
    renderList(container);
  } catch (err) {
    container.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
  }
}

export async function renderDevices(container) {
  container.innerHTML = '<div class="card" style="text-align:center;"><p>Cargando sitios...</p></div>';
  await loadSites(container);
}
