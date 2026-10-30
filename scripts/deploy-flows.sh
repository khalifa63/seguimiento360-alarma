#!/usr/bin/env bash
# Despliegue del flow de Node-RED (patron de ops: backup + restart + snapshot).
#
# Uso:  scripts/deploy-flows.sh [flow.json]     (default: /tmp/flows_new.json)
#
# Reglas que este script hace obligatorias (2026-10-30, ver
# docs/infraestructura/FLUJOS_NODERED.md):
#   1. Backup del flow vivo antes de tocar nada.
#   2. Validacion de JSON y de sintaxis de function nodes ANTES de publicar
#      (check-flows-funcs.py); un SyntaxError silencioso deja endpoints mudos.
#   3. Snapshot saneado (pass del broker enmascarado) hacia el repo.
#   4. La copia de trabajo /tmp/flows_live.json queda sincronizada con lo
#      vivo al final: patchear sobre una copia vieja revierte cambios
#      anteriores sin aviso (paso dos veces: PUT handler y agrupador del
#      overview).
set -euo pipefail
FLOW_IN="${1:-/tmp/flows_new.json}"
STAMP=$(date +%Y%m%d-%H%M%S)
FECHA=$(date +%Y%m%d)
REPO=~/seguimiento360-alarma

# 2. guardia de sintaxis y JSON antes de tocar produccion
python3 "$REPO/scripts/check-flows-funcs.py" "$FLOW_IN"

# 1. backup + despliegue + reinicio
docker exec nodered cp /data/flows.json "/data/flows.json.bak-$STAMP"
docker cp "$FLOW_IN" nodered:/data/flows.json
docker restart nodered >/dev/null
sleep 12
if docker exec nodered node -e 'require("/data/flows.json"); console.log("JSON valido")' 2>/dev/null; then
  echo "OK: flow desplegado"
else
  echo "JSON invalido: restaurando backup" >&2
  docker exec nodered cp "/data/flows.json.bak-$STAMP" /data/flows.json
  docker restart nodered >/dev/null
  exit 1
fi

# 3. snapshot saneado hacia el repo
docker cp nodered:/data/flows.json /tmp/flows_snapshot.json
python3 - <<EOF
import json
f = json.load(open('/tmp/flows_snapshot.json'))
for n in f:
    if n.get('type') == 'mqtt-broker' and isinstance(n.get('credentials'), dict) and 'pass' in n['credentials']:
        n['credentials']['pass'] = '***'
json.dump(f, open('$REPO/docs/infraestructura/snapshots/flows-produccion-$FECHA.json', 'w'), indent=1, ensure_ascii=False)
json.dump(f, open('$REPO/backend/nodered-flows/flows.json', 'w'), indent=1, ensure_ascii=False)
print('Snapshot saneado y sincronizado: flows-produccion-$FECHA.json')
EOF

# 4. la copia de trabajo queda igual a lo vivo
docker cp nodered:/data/flows.json /tmp/flows_live.json
echo "copia de trabajo /tmp/flows_live.json sincronizada con el flow vivo"
