#!/bin/bash
# Empaqueta la KB autorizada para Dify (comun/firmware/servidor)
cd "$(dirname "$0")/.." || exit 1
rm -rf dify-kb/comun dify-kb/firmware dify-kb/servidor
mkdir -p dify-kb/{comun,firmware,servidor}

# COMUN (ambos chats)
[ -f infraestructura_servidor.pdf ] && cp infraestructura_servidor.pdf dify-kb/comun/ || echo "WARN: PDF no esta en el repo; subelo manual"
cp docs/README.md dify-kb/comun/
cp docs/fases/RESUMEN_FASES.md dify-kb/comun/
cp docs/patrones/PATRONES_DISENO.md dify-kb/comun/
cp docs/hardware/ARQUITECTURA_HUB.md dify-kb/comun/
cp docs/hardware/PROTOCOLO_ESPNOW.md dify-kb/comun/
cp docs/infraestructura/FLUJOS_NODERED.md dify-kb/comun/
cp docs/marco-teorico/04_arquitectura_sistema.md dify-kb/comun/ 2>/dev/null
cp docs/marco-teorico/07_seguridad.md dify-kb/comun/ 2>/dev/null

# FIRMWARE (Chat A)
cp firmware/PROMPT-SDD-MASTER.md dify-kb/firmware/
cp firmware/hub/specs/*.md dify-kb/firmware/

# SERVIDOR (Chat B)
cp backend/PROMPT-SDD-SERVER.md dify-kb/servidor/
cp docs/infraestructura/ESTADO_ACTUAL.md dify-kb/servidor/ 2>/dev/null
cp docs/infraestructura/CLOUDFLARE_RUTAS.md dify-kb/servidor/ 2>/dev/null
cp docs/infraestructura/settings-nodered-reference.js dify-kb/servidor/ 2>/dev/null
cp backend/nodered-flows/flows.json dify-kb/servidor/

echo "KB empaquetada:"
find dify-kb -type f | sort
