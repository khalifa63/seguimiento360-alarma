#!/usr/bin/env bash
#
# restaurar-nodered.sh — Restaura un respaldo de Node-RED
#
# Uso:
#   ./restaurar-nodered.sh <directorio-del-respaldo>
#   ./restaurar-nodered.sh --ultimo
#   ./restaurar-nodered.sh --listar
#
# Restaura las TRES piezas que hacen falta:
#
#   flows.json        el flujo
#   flows_cred.json   las credenciales cifradas
#   settings.js       contiene la credentialSecret que descifra las anteriores
#
# Las credenciales de Node-RED se cifran con una clave que vive en settings.js.
# Restaurar flows_cred.json sin su settings.js correspondiente deja las
# credenciales ilegibles, y los nodos que las usan (el broker MQTT, entre
# otros) dejan de conectar. Por eso van juntas.
#
# Antes de sobrescribir nada se guarda el estado actual, de modo que la
# restauracion tambien se puede deshacer.

set -uo pipefail

RAIZ="${HOME}/backups-nodered"
CONTENEDOR="nodered"
URL="http://localhost:1880"

rojo()  { printf '\033[31m%s\033[0m\n' "$*"; }
verde() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }

listar() {
    echo "Respaldos disponibles en ${RAIZ}:"
    echo
    for d in "${RAIZ}"/restaurable-*; do
        [ -d "$d" ] || continue
        n=$(cat "$d/nodos.txt" 2>/dev/null || echo "?")
        f=$(stat -c%y "$d" 2>/dev/null | cut -d. -f1)
        etq=$(cat "$d/etiqueta.txt" 2>/dev/null || echo "")
        # Sin ancho fijo: los nombres con etiqueta son largos y desalinean.
        printf '  %s\n' "$(basename "$d")"
        printf '      %s nodos, %s%s\n' "$n" "$f" "${etq:+, $etq}"
    done
    echo
    echo "Para restaurar uno:   $0 <nombre-o-ruta>"
    echo "Para el mas reciente: $0 --ultimo"
}

comprobar_contenedor() {
    if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
        rojo "El contenedor '${CONTENEDOR}' no esta corriendo."
        exit 1
    fi
}

verificar_respaldo() {
    local d="$1"
    local faltan=0
    for f in flows.json flows_cred.json settings.js; do
        if [ ! -f "${d}/${f}" ]; then
            rojo "  Falta ${f} en el respaldo."
            faltan=1
        fi
    done
    [ "$faltan" -eq 0 ] || exit 1

    # Comprobar que flows.json es JSON valido: restaurar algo corrupto dejaria
    # Node-RED sin arrancar, y eso es peor que no restaurar.
    if ! python3 -c "import json; json.load(open('${d}/flows.json'))" 2>/dev/null; then
        rojo "  flows.json no es JSON valido."
        exit 1
    fi
    verde "  Respaldo verificado (3 archivos, JSON valido)"
}

restaurar() {
    local d="$1"
    local sello
    sello=$(date +%Y%m%d-%H%M%S)
    local antes="${RAIZ}/antes-de-restaurar-${sello}"

    echo "Restaurando desde: $(basename "$d")"
    echo

    # 1. Guardar el estado actual por si hay que volver atras
    info "Guardando el estado actual en antes-de-restaurar-${sello}..."
    mkdir -p "$antes"
    docker cp "${CONTENEDOR}:/data/flows.json"      "${antes}/flows.json"      2>/dev/null
    docker cp "${CONTENEDOR}:/data/flows_cred.json" "${antes}/flows_cred.json" 2>/dev/null
    docker cp "${CONTENEDOR}:/data/settings.js"     "${antes}/settings.js"     2>/dev/null
    info "  hecho"

    # 2. Parar Node-RED: escribir los archivos con el proceso vivo puede
    #    acabar en un flujo a medias si el contenedor los reescribe al vuelo.
    info "Parando Node-RED..."
    docker stop "$CONTENEDOR" >/dev/null 2>&1 || true

    # 3. Copiar los tres archivos
    for f in flows.json flows_cred.json settings.js; do
        if docker cp "${d}/${f}" "${CONTENEDOR}:/data/${f}" 2>/dev/null; then
            info "  restaurado: ${f}"
        else
            rojo "  FALLO al copiar ${f}"
            docker start "$CONTENEDOR" >/dev/null 2>&1
            exit 1
        fi
    done

    # 4. Arrancar
    info "Arrancando Node-RED..."
    docker start "$CONTENEDOR" >/dev/null 2>&1

    # 5. Esperar a que responda
    local intentos=0
    local codigo=""
    while [ "$intentos" -lt 30 ]; do
        codigo=$(curl -sS -o /dev/null -w '%{http_code}' --max-time 2 "${URL}/api/overview" 2>/dev/null)
        [ -n "$codigo" ] && [ "$codigo" != "000" ] && break
        sleep 1
        intentos=$((intentos + 1))
    done

    echo
    if [ "$intentos" -ge 30 ]; then
        rojo "Node-RED no responde tras 30 s. Revisa:  docker logs ${CONTENEDOR}"
        echo "Para volver atras:  $0 ${antes}"
        exit 1
    fi

    # 6. Comprobar que cargo el flujo esperado
    local nodos
    nodos=$(docker exec "$CONTENEDOR" python3 -c \
        "import json;print(len(json.load(open('/data/flows.json'))))" 2>/dev/null || echo "?")

    verde "Restaurado correctamente."
    info "nodos cargados: ${nodos}"
    info "GET /api/overview -> ${codigo}  (401 = el flujo responde)"

    if [ "$codigo" != "401" ]; then
        echo
        rojo "El endpoint no responde como se espera. Puede que el flujo no sea el correcto."
        echo "Para volver atras:  $0 ${antes}"
        exit 1
    fi

    echo
    info "Estado anterior guardado en: $(basename "$antes")"
}

case "${1:-}" in
    ""|-h|--help)
        sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    --listar)
        listar
        exit 0
        ;;
    --ultimo)
        comprobar_contenedor
        ULTIMO=$(ls -dt "${RAIZ}"/restaurable-* 2>/dev/null | head -1)
        if [ -z "$ULTIMO" ]; then
            rojo "No hay respaldos en ${RAIZ}"
            exit 1
        fi
        verificar_respaldo "$ULTIMO"
        restaurar "$ULTIMO"
        ;;
    *)
        comprobar_contenedor
        D="$1"
        [ -d "$D" ] || D="${RAIZ}/$1"
        if [ ! -d "$D" ]; then
            rojo "No existe el respaldo: $1"
            exit 1
        fi
        verificar_respaldo "$D"
        restaurar "$D"
        ;;
esac
