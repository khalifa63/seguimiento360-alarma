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
