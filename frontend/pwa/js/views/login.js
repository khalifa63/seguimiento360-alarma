import { Api } from '../api.js?v=__V__';
import { Auth } from '../auth.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

export function renderLogin(container) {
  const okFlag = sessionStorage.getItem('s360_just_registered');
  sessionStorage.removeItem('s360_just_registered');

  container.innerHTML = `
    <div class="card">
      <h2 style="text-align:center; margin-bottom:1rem;">Iniciar Sesión</h2>
      ${okFlag ? '<div class="alert alert-success">Cuenta creada correctamente. Ahora inicia sesión.</div>' : ''}
      <div id="login-alert"></div>
      <form id="login-form">
        <div class="input-group">
          <label for="login-email">Correo electrónico</label>
          <input type="email" id="login-email" required autocomplete="email" placeholder="tucorreo@ejemplo.com">
        </div>
        <div class="input-group">
          <label for="login-password">Contraseña</label>
          <input type="password" id="login-password" required autocomplete="current-password" placeholder="••••••••">
        </div>
        <button type="submit" class="btn" id="login-btn">Entrar</button>
      </form>
      <p style="text-align:center; margin-top:1rem; color:var(--text-secondary); font-size:0.9rem;">
        ¿Sin cuenta? <a href="#/register" style="color:var(--accent);">Regístrate</a>
      </p>
    </div>
  `;

  document.getElementById('login-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const alertBox = document.getElementById('login-alert');
    const btn = document.getElementById('login-btn');
    alertBox.innerHTML = '';
    btn.disabled = true;
    btn.textContent = 'Verificando...';
    try {
      const data = await Api.login({
        email: document.getElementById('login-email').value.trim(),
        password: document.getElementById('login-password').value
      });
      Auth.saveSession(data.token, data.user);
      window.location.hash = '#/dashboard';
    } catch (err) {
      alertBox.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
      btn.disabled = false;
      btn.textContent = 'Entrar';
    }
  });
}
