# Patrones de Diseño

## P1: Validación JWT en Node-RED

    const jwt = global.get('jwt');
    const jwtSecret = global.get('jwtSecret');
    const auth = (msg.req.headers['authorization'] || '');
    if (!auth.startsWith('Bearer ')) {
        msg.statusCode = 401;
        msg.payload = { error: 'No autorizado' };
        return [null, msg];
    }
    try {
        msg._user = jwt.verify(auth.slice(7), jwtSecret);
        return [msg, null];
    } catch(e) {
        msg.statusCode = 401;
        msg.payload = { error: 'Token inválido o expirado' };
        return [null, msg];
    }

NUNCA usar env.get('JWT_SECRET') ni libs para jwt (conflictua).

## P2: Gate de error (outputs: 2)

- Error:   return [null, msg]  -> output 2 -> s360v2_http_error
- Exito:   return [msg, null]  -> output 1 -> siguiente nodo
- wires:   [["nodo_siguiente"], ["s360v2_http_error"]]
- wires incompleto = request HTTP colgada (lección de 3.2.2a).

## P3: Update parcial con COALESCE

    UPDATE tabla SET campo = COALESCE($1, campo) WHERE ...;

Preparar params: null si el campo no viene en el body.

## P4: Claim codes con checksum

    CHARSET = '23456789ABCDEFGHJKMNPQRSTUVWXYZ'  (31 chars)
    base = prefijo(2) + random(6)
    checksum = CHARSET[ sumaASCII(base) % 31 ]
    codigo = prefijo + '-' + random + '-' + checksum   (11 chars)

Validar: longitud 11, guiones en posiciones 2 y 9, recalcular checksum.
Validar checksum ANTES del SELECT (anti-enumeración).

## P5: Multi-tenant estricto

- Todo SQL filtra por tenant_id = msg._user.tenant_id
- Claim codes: (tenant_id IS NULL OR tenant_id = $tenant)
- INSERT: tenant_id siempre del JWT, nunca del body

## P6: Edición inline en PWA

- Tarjeta <-> formulario con outerHTML
- Delegación de eventos: container.onclick
- Atributos data-edit / data-save / data-cancel
- escAttr() para valores en inputs; escapeHtml() para texto

## Anti-patrones (prohibidos)

- libs + global.get del mismo modulo en un nodo function
- return msg (sin array) en nodo con outputs:2
- SELECT con filtro de status para validar existencia
- UPDATE sin COALESCE con body parcial
- prompt() de una sola entrada para editar entidades multi-campo
- Triple backticks dentro de heredocs (rompe el copiado): usar indentación

## P7: Imports versionados en PWA

Todo import o recurso local lleva ?v=__V__ y deploy-pwa.sh lo
reemplaza por un timestamp en cada deploy:

    import { Api } from '../api.js?v=__V__';

Esto fuerza recarga en navegador y Service Worker tras cada deploy.

## P8: Limite de ancho global (mobile-first con tope en desktop)

Una sola regla en css/styles.css centra TODAS las vistas:

    #app-container {
      max-width: 720px;
      margin: 0 auto;
      padding: 1rem;
    }

Preferible a max-width inline por vista: consistente y mantenible.
En movil ocupa 100% (720px > ancho de pantalla); en desktop queda centrado.

## P9: Deploy obligatorio en bloques de frontend

Todo bloque que toca frontend/pwa/ termina SIEMPRE con:

    ./scripts/deploy-pwa.sh
    git add frontend/pwa
    git commit -m "..." && git push

Sin deploy el cambio vive en el repo pero no en el servidor.

## P10: Verificar exports antes de escribir una vista

Antes de importar funciones de un modulo existente:

    grep -n "export" frontend/pwa/js/api.js
    grep -n "export" frontend/pwa/js/auth.js
    grep -n "export" frontend/pwa/js/utils.js

El proyecto exporta objetos/funciones especificos:
- api.js: export const Api = { sites, createSite, hubs, alarmControl, ... }
- auth.js: export const Auth = { getToken, getUser, logout, ... }
- utils.js: export function escapeHtml(...)

## Anti-patrones agregados

- Escribir imports sin verificar exports reales del modulo
- Olvidar ./scripts/deploy-pwa.sh en bloques de frontend
- Crear routers paralelos sin revisar el entry point de index.html
  (caso: router.js huerfano vs app.js real)
- Asumir cache como causa sin probar primero en incognito

## P11: Nunca bypasear el router con preventDefault

Un enlace con e.preventDefault() que renderiza la vista directamente
deja el hash sin cambiar. Consecuencia: al asignar de nuevo el mismo
hash (location.hash = '#/dashboard') NO se dispara hashchange y el
boton volver parece muerto.

Regla: si la ruta existe en el router real (app.js), usar navegacion
por hash normal (a href="#/ruta"). El bypass solo es aceptable como
parche temporal y debe eliminarse al registrar la ruta oficial.
