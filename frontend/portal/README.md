# Portal de produccion (seguimiento360.com/portal)

Copia del portal desplegado en el contenedor `nginx`
(/usr/share/nginx/html/portal), tomada el 2026-10-29 y sin los ficheros
.bak-* de iteraciones previas.

- Servido por el contenedor nginx (server_name seguimiento360.com) en /portal/.
- Paginas: index.html (inicio), nodos.html (vista de nodos; consume /api/nodes/
  y /api/overview), eventos.html (ultimos eventos), dashboard.html,
  diagnostico.html, registro.html.
- El emparejamiento se guia desde la UI: boton BOOT del hub o la ventana de
  emparejamiento del panel (en el hub: PAIR_BUTTON_PIN GPIO_NUM_0 y
  pair_button_task).
- OJO: dashboard.html y eventos.html renderizan `distance_cm`; cuando aplique
  el cambio de contrato del payload v1 (byte 6 -> `value`), actualizar ambas
  vistas para que un evento DOOR muestre Abierto/Cerrado.

frontend/pwa/ es una iteracion modular distinta, no desplegada en /portal/.
