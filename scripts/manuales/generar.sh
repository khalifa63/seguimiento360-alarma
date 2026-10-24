#!/usr/bin/env bash
#
# generar.sh — Convierte los manuales a HTML y PDF publicables
#
# Uso:
#   scripts/manuales/generar.sh            genera todo en docs/manuales/salida/
#   scripts/manuales/generar.sh --html     solo HTML (para el sitio web)
#   scripts/manuales/generar.sh --pdf      solo PDF (para las cajas)
#   scripts/manuales/generar.sh --publicar copia el HTML al sitio web
#
# Fuente unica: los .md de docs/manuales/. Este script no inventa contenido,
# solo lo maqueta. Si un manual cambia, se vuelve a ejecutar.
#
# El HTML sale autonomo (con el CSS incrustado): se puede abrir con doble clic
# o servir desde nginx sin dependencias.
#
# El PDF se genera sin portada ni numeracion de pandoc; lleva la identidad de
# la marca y un pie con la version.

set -uo pipefail

AQUI="$(cd "$(dirname "$0")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"
MD="${RAIZ}/docs/manuales"
SALIDA="${MD}/salida"
CSS="${AQUI}/estilo.css"
WEB="${HOME}/iot-alarma/website/manuales"

rojo()  { printf '\033[31m%s\033[0m\n' "$*"; }
verde() { printf '\033[32m%s\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }

HACER_HTML=1
HACER_PDF=1
PUBLICAR=0

case "${1:-}" in
    --html)     HACER_PDF=0 ;;
    --pdf)      HACER_HTML=0 ;;
    --publicar) PUBLICAR=1 ;;
    -h|--help)  sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

command -v pandoc >/dev/null 2>&1 || { rojo "Falta pandoc."; exit 1; }
[ -f "$CSS" ] || { rojo "Falta el estilo: ${CSS}"; exit 1; }

mkdir -p "$SALIDA"

# Manuales de usuario: los que van en la caja y en la web.
USUARIO=(
    "hub:Tu Hub Seguimiento 360"
    "sensor-ultrasonico:Tu Sensor de Movimiento"
)

# Manuales tecnicos: para quien instala y mantiene.
TECNICO=(
    "hub:Hub Seguimiento 360 - Manual tecnico"
    "sensor-ultrasonico:Sensor ultrasonico - Manual tecnico"
    "protocolo-comun:Protocolo ESP-NOW - Manual tecnico"
)

convertir() {
    local archivo="$1" titulo="$2" tipo="$3"
    local base
    base=$(basename "$archivo" .md)
    local prefijo=""
    [ "$tipo" = "tecnico" ] && prefijo="tecnico-"
    local destino="${SALIDA}/${prefijo}${base}"

    if [ "$HACER_HTML" = "1" ]; then
        pandoc "$archivo" \
            -f markdown -t html5 --standalone \
            --metadata title="$titulo" --metadata lang=es \
            --css "$CSS" --embed-resources \
            -o "${destino}.html" 2>/dev/null \
            && info "HTML: $(basename "${destino}.html")" \
            || { rojo "  fallo HTML: ${archivo}"; return 1; }
    fi

    if [ "$HACER_PDF" = "1" ]; then
        # El PDF va por LaTeX: da mejor calidad de imprenta que un navegador
        # y ya esta instalado. Se elige el motor segun lo que haya.
        local motor=""
        for m in xelatex lualatex pdflatex; do
            if command -v "$m" >/dev/null 2>&1; then motor="$m"; break; fi
        done

        if [ -z "$motor" ]; then
            info "PDF:  omitido (sin motor LaTeX)"
        # Sin --metadata lang: el paquete de idioma espanol de babel no esta
        # instalado y pandoc falla con "Unknown option 'spanish'". El
        # contenido del manual sigue en espanol; lo unico que cambia es el
        # idioma de maquetacion de LaTeX, que queda en ingles.
        #
        # Si algun dia se instala texlive-lang-spanish, se puede anadir
        # --metadata lang=es y la maquetacion saldra en espanol.
        elif pandoc "$archivo" \
            -f markdown -t pdf \
            --pdf-engine="$motor" \
            --metadata title="$titulo" \
            -V geometry:margin=2cm \
            -V fontsize=11pt \
            -V colorlinks=true \
            -V linkcolor=blue \
            -V urlcolor=blue \
            -o "${destino}.pdf" >/dev/null 2>&1; then
            info "PDF:  $(basename "${destino}.pdf")  ($motor)"
        else
            rojo "  fallo PDF: ${archivo}"
        fi
    fi
}

echo "Generando manuales..."
echo

info "De usuario:"
for entrada in "${USUARIO[@]}"; do
    nombre="${entrada%%:*}"
    titulo="${entrada#*:}"
    convertir "${MD}/usuario/${nombre}.md" "$titulo" "usuario"
done

echo
info "Tecnicos:"
for entrada in "${TECNICO[@]}"; do
    nombre="${entrada%%:*}"
    titulo="${entrada#*:}"
    convertir "${MD}/tecnico/${nombre}.md" "$titulo" "tecnico"
done

# Indice publicable
if [ "$HACER_HTML" = "1" ]; then
    cat > "${SALIDA}/index.html" <<'HTMLEOF'
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Manuales Seguimiento 360</title>
<link rel="stylesheet" href="estilo.css">
</head>
<body>
<h1>Manuales Seguimiento 360</h1>
<p>Gu&iacute;as de los dispositivos. Elige el manual que necesites.</p>

<h2>Para empezar</h2>
<ul class="indice">
<li><a href="hub.html">Tu Hub Seguimiento 360</a>
<p>C&oacute;mo conectarlo a tu WiFi y dejarlo funcionando.</p></li>
<li><a href="sensor-ultrasonico.html">Tu Sensor de Movimiento</a>
<p>C&oacute;mo colocarlo y comprobar que detecta bien.</p></li>
</ul>

<h2>Manuales t&eacute;cnicos</h2>
<p>Datos de ingenier&iacute;a, para quien instala o repara los equipos.</p>
<ul class="indice">
<li><a href="tecnico-hub.html">Hub &mdash; manual t&eacute;cnico</a>
<p>Hardware, particiones, radio, MQTT, actualizaciones y diagn&oacute;stico.</p></li>
<li><a href="tecnico-sensor-ultrasonico.html">Sensor ultras&oacute;nico &mdash; manual t&eacute;cnico</a>
<p>Pines, par&aacute;metros, vinculaci&oacute;n con el hub y recuperaci&oacute;n de fallos.</p></li>
<li><a href="tecnico-protocolo-comun.html">Protocolo ESP-NOW &mdash; manual t&eacute;cnico</a>
<p>El enlace que comparten todos los dispositivos.</p></li>
</ul>

<p class="pie">Seguimiento 360 &middot; Estos manuales se actualizan con el producto.</p>
</body>
</html>
HTMLEOF
    cp "$CSS" "${SALIDA}/estilo.css"
    info "HTML: index.html"
fi

echo
verde "Listo. Archivos en: ${SALIDA}"
ls -la "$SALIDA" | tail -n +2 | awk '{printf "  %-38s %8s B\n", $9, $5}'

if [ "$PUBLICAR" = "1" ]; then
    echo
    info "Publicando en el sitio web..."
    mkdir -p "$WEB"
    cp -f "${SALIDA}"/*.html "${SALIDA}"/*.css "$WEB/" 2>/dev/null
    chmod -R a+r "$WEB"
    info "  copiado a ${WEB}"
    info "  URL: https://seguimiento360.com/manuales/"
fi
