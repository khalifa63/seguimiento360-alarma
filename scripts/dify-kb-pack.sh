#!/bin/bash
# Empaqueta la KB autorizada para Dify (comun/firmware/servidor).
# El resultado es un artefacto generado: no se versiona (ver .gitignore).
set -euo pipefail

cd "$(dirname "$0")/.." || exit 1

rm -rf dify-kb/comun dify-kb/firmware dify-kb/servidor
mkdir -p dify-kb/comun dify-kb/firmware dify-kb/servidor

# COMUN (ambos chats)
if [ -f infraestructura_servidor.pdf ]; then
    cp infraestructura_servidor.pdf dify-kb/comun/
else
    echo "WARN: infraestructura_servidor.pdf no está en el repo; súbelo manualmente"
fi
cp docs/README.md dify-kb/comun/
cp docs/00-vision.md dify-kb/comun/
cp docs/01-manual-general.md dify-kb/comun/
cp docs/fases/RESUMEN_FASES.md dify-kb/comun/
cp docs/patrones/PATRONES_DISENO.md dify-kb/comun/
cp docs/hardware/ARQUITECTURA_HUB.md dify-kb/comun/
cp docs/hardware/PROTOCOLO_ESPNOW.md dify-kb/comun/
cp docs/infraestructura/FLUJOS_NODERED.md dify-kb/comun/
cp docs/marco-teorico/04_arquitectura_sistema.md dify-kb/comun/
cp docs/marco-teorico/07_seguridad.md dify-kb/comun/

# FIRMWARE (Chat A)
cp docs/gobernanza/PROMPT-SDD-MASTER.md dify-kb/firmware/
cp docs/firmware/specs/*.md dify-kb/firmware/

# SERVIDOR (Chat B)
cp docs/gobernanza/PROMPT-SDD-SERVER.md dify-kb/servidor/
cp docs/infraestructura/ESTADO_ACTUAL.md dify-kb/servidor/
cp docs/infraestructura/CLOUDFLARE_RUTAS.md dify-kb/servidor/
cp backend/nodered-flows/flows.json dify-kb/servidor/

echo "KB empaquetada:"
find dify-kb -type f | sort
