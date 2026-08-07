import { CONFIG } from './config.js?v=__V__';
import { Auth } from './auth.js?v=__V__';

async function request(path, options) {
  const opts = options || {};
  const headers = { 'Content-Type': 'application/json' };

  if (opts.auth) {
    const token = Auth.getToken();
    if (token) headers['Authorization'] = 'Bearer ' + token;
  }

  let res;
  try {
    res = await fetch(CONFIG.API_BASE + path, {
      method: opts.method || 'GET',
      headers: headers,
      body: opts.body ? JSON.stringify(opts.body) : null
    });
  } catch (e) {
    throw new Error('Sin conexión con el servidor. Verifica tu internet.');
  }

  let data = null;
  try { data = await res.json(); } catch (e) { /* cuerpo vacio */ }

  if (res.status === 401 && opts.auth) {
    Auth.logout();
    throw new Error('Sesión expirada. Inicia sesión nuevamente.');
  }

  if (!res.ok) {
    const err = new Error((data && data.error) ? data.error : 'Error ' + res.status);
    err.status = res.status;
    err.data = data;
    throw err;
  }
  return data;
}

export const Api = {
  register:   (payload) => request('/api/auth/register', { method: 'POST', body: payload }),
  login:      (payload) => request('/api/auth/login',    { method: 'POST', body: payload }),
  profile:    ()        => request('/api/auth/profile',  { auth: true }),

  // Sitios (Fase 3.2.1)
  sites:      ()        => request('/api/sites',         { auth: true }),
  createSite: (payload) => request('/api/sites',         { auth: true, method: 'POST', body: payload }),
  updateSite: (id, payload) => request('/api/sites/' + id, { auth: true, method: 'PUT', body: payload }),

  // Dispositivos (Fase 3.1 + 3.2.2)
  devices:    ()        => request('/api/devices',       { auth: true }),
  claimDevice:(payload) => request('/api/claim',         { auth: true, method: 'POST', body: payload }),

  // Admin (Fase 3.2.2b)
  generateCodes: (payload) => request('/api/admin/generate-codes', { auth: true, method: 'POST', body: payload }),
  claimCodes:    ()        => request('/api/admin/claim-codes',    { auth: true })
};
