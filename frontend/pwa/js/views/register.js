import { Api } from '../api.js?v=__V__';
import { escapeHtml } from '../utils.js?v=__V__';

export function renderRegister(container) {
  container.innerHTML = `
    <div class="card">
      <h2 style="text-align:center; margin-bottom:1rem;">Crear Cuenta</h2>
      <div id="reg-alert"></div>
      <form id="reg-form">
        <div class="input-group">
          <label for="reg-name">Nombre completo</label>
          <input type="text" id="reg-name" required autocomplete="name" placeholder="Antonio Pérez">
        </div>
        <div class="input-group">
          <label for="reg-email">Correo electrónico</label>
          <input type="email" id="reg-email" required autocomplete="email" placeholder="tucorreo@ejemplo.com">
        </div>
        <div class="input-group">
          <label for="reg-phone">Teléfono (opcional)</label>
          <input type="tel" id="reg-phone" autocomplete="tel" placeholder="+52 55 0000 0000">
        </div>
        <div class="input-group">
          <label for="reg-pass">Contraseña (mínimo 8 caracteres)</label>
          <input type="password" id="reg-pass" required minlength="8" autocomplete="new-password" placeholder="••••••••">
        </div>
        <div class="input-group">
          <label for="reg-pass2">Confirmar contraseña</label>
          <input type="password" id="reg-pass2" required minlength="8" autocomplete="new-password" placeholder="••••••••">
        </div>
        <button type="submit" class="btn" id="reg-btn">Registrarme</button>
      </form>
      <p style="text-align:center; margin-top:1rem; color:var(--text-secondary); font-size:0.9rem;">
        ¿Ya tienes cuenta? <a href="#/login" style="color:var(--accent);">Inicia sesión</a>
      </p>
    </div>
  `;

  document.getElementById('reg-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const alertBox = document.getElementById('reg-alert');
    const btn = document.getElementById('reg-btn');
    alertBox.innerHTML = '';

    const pass = document.getElementById('reg-pass').value;
    const pass2 = document.getElementById('reg-pass2').value;

    if (pass.length < 8) {
      alertBox.innerHTML = '<div class="alert alert-error">La contraseña debe tener al menos 8 caracteres.</div>';
      return;
    }
    if (pass !== pass2) {
      alertBox.innerHTML = '<div class="alert alert-error">Las contraseñas no coinciden.</div>';
      return;
    }

    btn.disabled = true;
    btn.textContent = 'Creando cuenta...';
    try {
      await Api.register({
        email: document.getElementById('reg-email').value.trim(),
        password: pass,
        full_name: document.getElementById('reg-name').value.trim(),
        phone: document.getElementById('reg-phone').value.trim()
      });
      sessionStorage.setItem('s360_just_registered', '1');
      window.location.hash = '#/login';
    } catch (err) {
      alertBox.innerHTML = '<div class="alert alert-error">' + escapeHtml(err.message) + '</div>';
      btn.disabled = false;
      btn.textContent = 'Registrarme';
    }
  });
}
