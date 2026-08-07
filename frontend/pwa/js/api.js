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
    throw err;
  }
  return data;
}

export const Api = {
  register: (payload) => request('/api/auth/register', { method: 'POST', body: payload }),
  login:    (payload) => request('/api/auth/login',    { method: 'POST', body: payload }),
  profile:  ()        => request('/api/auth/profile',  { auth: true })
};
