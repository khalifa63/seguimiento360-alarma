#!/usr/bin/env bash
#
# respaldar-nodered.sh — Copia de seguridad restaurable de Node-RED
#
# Uso:
#   ./respaldar-nodered.sh              crear un respaldo
#   ./respaldar-nodered.sh --etiqueta antes-de-prueba-ota
#   ./respaldar-nodered.sh --listar     ver los que hay
#   ./respaldar-nodered.sh --podar 5    conservar solo los 5 mas recientes
#
# Copia los TRES archivos que hacen falta para volver atras:
#
#   flows.json        el flujo
#   flows_cred.json   las credenciales cifradas
#   settings.js       contiene la credentialSecret que descifra las anteriores
#
# Copiar solo el flujo no basta: sin el settings.js correspondiente, las
# credenciales restauradas quedan ilegibles y los nodos que las usan dejan de
# conectar. El sintoma no aparece hasta que algo intenta publicar.
#
# Verifica lo copiado con md5 antes de darlo por bueno. Un respaldo que no se
# ha comprobado no es un respaldo.
#
# Restaurar:  scripts/restaurar-nodered.sh --ultimo

set -uo pipefail

RAIZ="${HOME}/backups-nodered"
CONTENEDOR="nodered"
ARCHIVOS=(flows.json flows_cred.json settings.js)

rojo()  { printf '\033[31m%s\033[0m\n' "$*"; }
verde() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }

listar() {
    echo "Respaldos en ${RAIZ}:"
    echo
    local hay=0
    for d in $(ls -dt "${RAIZ}"/restaurable-* 2>/dev/null); do
        [ -d "$d" ] || continue
        hay=1
        local n f etq
        n=$(cat "$d/nodos.txt" 2>/dev/null || echo "?")
        f=$(stat -c%y "$d" 2>/dev/null | cut -d. -f1)
        etq=$(cat "$d/etiqueta.txt" 2>/dev/null || echo "")
        printf '  %-44s %4s nodos  %s  %s\n' "$(basename "$d")" "$n" "$f" "$etq"
    done
    [ "$hay" -eq 1 ] || echo "  (ninguno)"
    echo
}

podar() {
    local conservar="${1:-5}"
    local todos
    todos=$(ls -dt "${RAIZ}"/restaurable-* 2>/dev/null)
    local total=0
    for d in $todos; do
        total=$((total + 1))
        if [ "$total" -gt "$conservar" ]; then
            rm -rf "$d"
            info "eliminado: $(basename "$d")"
        fi
    done
    [ "$total" -gt "$conservar" ] || info "nada que podar (hay ${total})"
}

# --- principal ---
case "${1:-}" in
    --listar) listar; exit 0 ;;
    --podar)  podar "${2:-5}"; exit 0 ;;
    -h|--help)
        sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
esac

ETIQUETA=""
if [ "${1:-}" = "--etiqueta" ]; then
    ETIQUETA="${2:-}"
    [ -n "$ETIQUETA" ] || { rojo "Falta el texto de la etiqueta."; exit 1; }
fi

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
    rojo "El contenedor '${CONTENEDOR}' no esta corriendo. No hay nada que copiar."
    exit 1
fi

SELLO=$(date +%Y%m%d-%H%M%S)
[ -n "$ETIQUETA" ] && SELLO="${SELLO}-${ETIQUETA}"
DESTINO="${RAIZ}/restaurable-${SELLO}"
mkdir -p "$DESTINO"

echo "Creando respaldo: $(basename "$DESTINO")"
echo

# 1. Copiar los tres archivos
for f in "${ARCHIVOS[@]}"; do
    if docker cp "${CONTENEDOR}:/data/${f}" "${DESTINO}/${f}" 2>/dev/null; then
        info "copiado: ${f}  ($(stat -c%s "${DESTINO}/${f}") B)"
    else
        rojo "FALLO al copiar ${f}"
        rm -rf "$DESTINO"
        exit 1
    fi
done

# 2. Verificar contra el original. Comparar con el contenedor y no solo
#    calcular el md5 local: asi se detecta una copia truncada.
echo
info "Verificando..."
FALLOS=0
for f in "${ARCHIVOS[@]}"; do
    a=$(docker exec "$CONTENEDOR" md5sum "/data/${f}" 2>/dev/null | cut -d' ' -f1)
    b=$(md5sum "${DESTINO}/${f}" 2>/dev/null | cut -d' ' -f1)
    if [ -n "$a" ] && [ "$a" = "$b" ]; then
        info "  ${f}: OK"
    else
        rojo "  ${f}: NO COINCIDE (${a:-vacio} vs ${b:-vacio})"
        FALLOS=$((FALLOS + 1))
    fi
done

if [ "$FALLOS" -gt 0 ]; then
    rojo "El respaldo no es fiable. Se elimina."
    rm -rf "$DESTINO"
    exit 1
fi

# 3. Metadatos
docker exec "$CONTENEDOR" python3 -c \
    "import json;print(len(json.load(open('/data/flows.json'))))" \
    > "${DESTINO}/nodos.txt" 2>/dev/null || echo "?" > "${DESTINO}/nodos.txt"
md5sum "${DESTINO}"/flows.json "${DESTINO}"/flows_cred.json "${DESTINO}"/settings.js \
    > "${DESTINO}/MD5SUMS" 2>/dev/null
[ -n "$ETIQUETA" ] && echo "$ETIQUETA" > "${DESTINO}/etiqueta.txt"

echo
verde "Respaldo correcto."
info "nodos: $(cat "${DESTINO}/nodos.txt")"
info "ruta:  ${DESTINO}"
echo
info "Para restaurarlo:  scripts/restaurar-nodered.sh $(basename "$DESTINO")"
