#!/usr/bin/env bash
# Despliega archivos individuales del portal (repo -> website/portal).
#
# Uso:  scripts/deploy-portal.sh <archivo> [archivo...]
#       scripts/deploy-portal.sh nodos.html
#
# nginx sirve el portal desde ~/iot-alarma/website/portal, NO desde el repo
# (docs/infraestructura/RUTAS-ARCHIVOS.md). Todo cambio de portal requiere
# este paso, o el usuario seguira viendo la version vieja.
#
# Solo copia los archivos pedidos, con backup: la copia servida puede tener
# drift operativo y un rsync completo podria pisarlo.
set -euo pipefail
SRC=~/seguimiento360-alarma/frontend/portal
DST=~/iot-alarma/website/portal
STAMP=$(date +%Y%m%d-%H%M%S)

[ $# -ge 1 ] || { echo "uso: deploy-portal.sh <archivo...>"; exit 1; }
for f in "$@"; do
  [ -f "$SRC/$f" ] || { echo "no existe en el repo: $SRC/$f"; exit 1; }
  if [ -f "$DST/$f" ]; then
    cp "$DST/$f" "$DST/$f.bak-$STAMP"
  fi
  cp "$SRC/$f" "$DST/$f"
  echo "OK: $f desplegado (backup: $f.bak-$STAMP)"
done
