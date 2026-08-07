import { Auth } from './auth.js?v=__V__';
import { renderLogin } from './views/login.js?v=__V__';
import { renderRegister } from './views/register.js?v=__V__';
import { renderDashboard } from './views/dashboard.js?v=__V__';
import { renderDevices } from './views/devices.js?v=__V__';

const routes = {
  '/login': renderLogin,
  '/register': renderRegister,
  '/dashboard': renderDashboard,
  '/devices': renderDevices
};

function router() {
  const container = document.getElementById('app-container');
  let path = window.location.hash.replace('#', '') || '/';
  const authed = Auth.isTokenValid();

  if (path === '/') {
    window.location.hash = authed ? '#/dashboard' : '#/login';
    return;
  }
  if (['/dashboard', '/devices'].includes(path) && !authed) {
    window.location.hash = '#/login';
    return;
  }
  if (['/login', '/register'].includes(path) && authed) {
    window.location.hash = '#/dashboard';
    return;
  }

  const render = routes[path] || renderLogin;
  render(container);
}

window.addEventListener('hashchange', router);
document.addEventListener('DOMContentLoaded', router);
