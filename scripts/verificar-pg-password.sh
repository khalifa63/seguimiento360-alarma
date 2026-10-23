#!/usr/bin/env bash
# ============================================================================
# Verifica que la contraseña de PostgreSQL coincida con la del .env
#
# Por qué hace falta:
#   POSTGRES_PASSWORD solo se aplica en el PRIMER arranque del contenedor.
#   Si el volumen ya existe, la variable se ignora y la contraseña real
#   queda desincronizada del .env sin que nada avise. Eso rompe Node-RED,
#   que lee PG_PASSWORD del entorno.
#
# Uso:
#   ./scripts/verificar-pg-password.sh          # solo comprueba
#   ./scripts/verificar-pg-password.sh --fix    # corrige si hay diferencias
#
# Requiere: docker, acceso al .env del proyecto.
# ============================================================================
set -euo pipefail

ENV_FILE="${ENV_FILE:-$HOME/iot-alarma/.env}"
PG_CONTAINER="${PG_CONTAINER:-postgres}"
PG_USER="${PG_USER:-admin}"
PG_DB="${PG_DB:-seguimiento360_alarmas}"
NR_CONTAINER="${NR_CONTAINER:-nodered}"
FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

if [ ! -r "$ENV_FILE" ]; then
    echo "ERROR: no se puede leer $ENV_FILE" >&2
    exit 1
fi

# La contraseña puede tener caracteres especiales; se lee literal.
PG_PASS="$(grep -m1 '^POSTGRES_PASSWORD=' "$ENV_FILE" | cut -d= -f2-)"
if [ -z "$PG_PASS" ]; then
    echo "ERROR: POSTGRES_PASSWORD no definida en $ENV_FILE" >&2
    exit 1
fi
echo "Contraseña del .env: ${#PG_PASS} caracteres"

# ---------------------------------------------------------------------------
# La comprobación se hace DESDE OTRO CONTENEDOR a propósito.
#
# pg_hba.conf trae 'trust' para 127.0.0.1, así que una prueba desde dentro
# del contenedor de Postgres da OK aunque la contraseña sea incorrecta.
# Solo una conexión desde fuera valida la credencial de verdad.
# ---------------------------------------------------------------------------
echo -n "Conexión desde $NR_CONTAINER: "
if docker exec "$NR_CONTAINER" node -e "
const { Client } = require('/data/node_modules/pg');
const c = new Client({
  host: 'postgres', port: 5432, database: '$PG_DB',
  user: '$PG_USER', password: process.env.PG_PASSWORD
});
c.connect()
 .then(() => { console.log('OK'); return c.end(); })
 .catch(e => { console.log('FALLA: ' + e.message); process.exit(1); });
" 2>/dev/null; then
    echo "La contraseña está sincronizada. Nada que hacer."
    exit 0
fi

echo
echo "DESINCRONIZADA: el .env no coincide con la contraseña real del rol."

if [ "$FIX" -eq 0 ]; then
    echo
    echo "Para corregirlo:  $0 --fix"
    echo
    echo "Esto aplica la contraseña del .env al rol '$PG_USER'."
    echo "Aviso: si el volumen de Postgres se recrea, la corrección se pierde"
    echo "y hay que volver a ejecutar este script."
    exit 1
fi

echo
echo "Aplicando ALTER USER..."
docker exec "$PG_CONTAINER" psql -U "$PG_USER" -d postgres \
    -c "ALTER USER $PG_USER WITH PASSWORD '$PG_PASS';"

echo
echo -n "Verificando de nuevo desde $NR_CONTAINER: "
docker exec "$NR_CONTAINER" node -e "
const { Client } = require('/data/node_modules/pg');
const c = new Client({
  host: 'postgres', port: 5432, database: '$PG_DB',
  user: '$PG_USER', password: process.env.PG_PASSWORD
});
c.connect()
 .then(() => { console.log('OK'); return c.end(); })
 .catch(e => { console.log('FALLA: ' + e.message); process.exit(1); });
" 2>/dev/null
echo
echo "Reinicia Node-RED para que reconstruya el pool de conexiones:"
echo "  docker restart $NR_CONTAINER"
