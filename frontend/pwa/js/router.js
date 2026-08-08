import { renderLogin } from './views/login.js?v=__V__';
import { renderRegister } from './views/register.js?v=__V__';
import { renderDashboard } from './views/dashboard.js?v=__V__';
import { renderDevices } from './views/devices.js?v=__V__';
import { renderAlarmControl } from './views/alarm-control.js?v=__V__';
import { Auth } from './auth.js?v=__V__';

const routes = {
  '#/login':    { view: renderLogin,    auth: false },
  '#/register': { view: renderRegister, auth: false },
  '#/dashboard':{ view: renderDashboard, auth: true },
  '#/devices':  { view: renderDevices,  auth: true },
  '#/alarm-control': { view: renderAlarmControl, auth: true }
};

export function initRouter() {
  window.addEventListener('hashchange', route);
  route();
}

function route() {
  const hash = location.hash || '#/login';
  const app = document.getElementById('app');
  const route = routes[hash];

  if (!route) {
    location.hash = '#/login';
    return;
  }

  if (route.auth && !Auth.isAuthenticated()) {
    location.hash = '#/login';
    return;
  }

  if (!route.auth && Auth.isAuthenticated() && (hash === '#/login' || hash === '#/register')) {
    location.hash = '#/dashboard';
    return;
  }

  route.view(app);
}
