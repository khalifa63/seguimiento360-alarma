#!/bin/bash
# ============================================================
# BACKUP COMPLETO - SEGUIMIENTO 360 (CORREGIDO)
# Respalda: Firmware Hub, Firmware Nodo, Flujos Node-RED
# ============================================================

set -euo pipefail

# Configuración
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_ROOT=~/backups/seguimiento360
BACKUP_DIR="${BACKUP_ROOT}/backup_${TIMESTAMP}"
LOG_FILE="${BACKUP_DIR}/backup.log"

# Colores para terminal
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# CORRECCIÓN: Crear el directorio base ANTES de intentar escribir el log
mkdir -p "$BACKUP_DIR"

# Función de logging
log() {
    echo -e "${GREEN}[$(date '+%H:%M:%S')]${NC} $1" | tee -a "$LOG_FILE"
}

warn() {
    echo -e "${YELLOW}[$(date '+%H:%M:%S')]⚠️  $1${NC}" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED}[$(date '+%H:%M:%S')] $1${NC}" | tee -a "$LOG_FILE"
    exit 1
}

# ============================================================
# 1. CREAR ESTRUCTURA DE DIRECTORIOS
# ============================================================
log " Iniciando backup completo de Seguimiento 360..."
log "📁 Directorio de backup: ${BACKUP_DIR}"

mkdir -p "${BACKUP_DIR}/firmware/hub"
mkdir -p "${BACKUP_DIR}/firmware/node"
mkdir -p "${BACKUP_DIR}/nodered"
mkdir -p "${BACKUP_DIR}/config"
mkdir -p "${BACKUP_DIR}/docs"

# ============================================================
# 2. BACKUP FIRMWARE DEL HUB
# ============================================================
log "📡 Respaldando firmware del HUB..."

HUB_SRC=~/seguimiento360-alarma/firmware/hub
if [ -d "$HUB_SRC" ]; then
    cp -r "$HUB_SRC" "${BACKUP_DIR}/firmware/hub/"
    
    if [ -f "${HUB_SRC}/build/seguimiento360_hub.bin" ]; then
        cp "${HUB_SRC}/build/seguimiento360_hub.bin" "${BACKUP_DIR}/firmware/hub/"
        log "   ✅ Binario compilado respaldado"
    fi
    
    if [ -f "${HUB_SRC}/partitions.csv" ]; then cp "${HUB_SRC}/partitions.csv" "${BACKUP_DIR}/firmware/hub/"; fi
    if [ -f "${HUB_SRC}/sdkconfig" ]; then cp "${HUB_SRC}/sdkconfig" "${BACKUP_DIR}/firmware/hub/"; fi
    
    log "   ✅ Firmware del Hub respaldado correctamente"
else
    warn "   Directorio del firmware del Hub no encontrado: ${HUB_SRC}"
fi

# ============================================================
# 3. BACKUP FIRMWARE DEL NODO
# ============================================================
log "📡 Respaldando firmware del NODO..."

NODE_SRC=~/seguimiento360-alarma/firmware/sensor-ultrasonic
if [ -d "$NODE_SRC" ]; then
    cp -r "$NODE_SRC" "${BACKUP_DIR}/firmware/node/"
    if [ -f "${NODE_SRC}/build/seguimiento360_node.bin" ]; then
        cp "${NODE_SRC}/build/seguimiento360_node.bin" "${BACKUP_DIR}/firmware/node/"
    fi
    log "   ✅ Firmware del Nodo respaldado correctamente"
else
    warn "   Directorio del firmware del Nodo no encontrado: ${NODE_SRC}"
fi

# ============================================================
# 4. BACKUP FLOWS DE NODE-RED
# ============================================================
log "🔧 Respaldando flujos de Node-RED..."

if docker ps --format '{{.Names}}' | grep -q "^nodered$"; then
    docker exec nodered cp /data/flows.json /data/flows.json.backup_${TIMESTAMP}
    docker cp nodered:/data/flows.json "${BACKUP_DIR}/nodered/flows.json"
    
    if [ -f ~/iot-alarma/nodered/data/settings.js ]; then
        cp ~/iot-alarma/nodered/data/settings.js "${BACKUP_DIR}/nodered/settings.js"
    fi
    
    if [ -f ~/iot-alarma/nodered/data/flows_cred.json ]; then
        cp ~/iot-alarma/nodered/data/flows_cred.json "${BACKUP_DIR}/nodered/flows_cred.json"
        log "   ⚠️  Credenciales respaldadas (archivo sensible)"
    fi
    
    log "   ✅ Flujos de Node-RED respaldados correctamente"
else
    error "   Contenedor Node-RED no encontrado o no está corriendo"
fi

# ============================================================
# 5. BACKUP CONFIGURACIÓN DOCKER COMPOSE
# ============================================================
log "🐳 Respaldando configuración Docker..."

if [ -f ~/iot-alarma/docker-compose.yml ]; then
    cp ~/iot-alarma/docker-compose.yml "${BACKUP_DIR}/config/"
    log "   ✅ docker-compose.yml respaldado"
fi

if [ -f ~/iot-alarma/.env ]; then
    cp ~/iot-alarma/.env "${BACKUP_DIR}/config/"
    log "   ⚠️  Variables de entorno respaldadas (archivo sensible)"
fi

# ============================================================
# 6. BACKUP DOCUMENTACIÓN
# ============================================================
log "📚 Respaldando documentación..."

if [ -d ~/seguimiento360-alarma/docs ]; then
    cp -r ~/seguimiento360-alarma/docs "${BACKUP_DIR}/docs/"
    log "   ✅ Documentación respaldada"
fi

# ============================================================
# 7. GENERAR RESUMEN Y VERIFICACIÓN
# ============================================================
log "📊 Generando resumen del backup..."

BACKUP_SIZE=$(du -sh "$BACKUP_DIR" | cut -f1)
FILE_COUNT=$(find "$BACKUP_DIR" -type f | wc -l)

cat << SUMMARY > "${BACKUP_DIR}/RESUMEN.txt"
========================================
BACKUP SEGUIMIENTO 360
========================================
Fecha: $(date)
Timestamp: ${TIMESTAMP}
Tamaño total: ${BACKUP_SIZE}
Archivos respaldados: ${FILE_COUNT}
========================================
SUMMARY

log "📋 Resumen generado en: ${BACKUP_DIR}/RESUMEN.txt"

# ============================================================
# 8. CREAR ARCHIVO COMPRIMIDO
# ============================================================
log "🗜️  Creando archivo comprimido..."

cd ~/backups/seguimiento360
tar -czf "backup_${TIMESTAMP}.tar.gz" "backup_${TIMESTAMP}/"
COMPRESSED_SIZE=$(du -sh "backup_${TIMESTAMP}.tar.gz" | cut -f1)

log "✅ Backup comprimido: backup_${TIMESTAMP}.tar.gz (${COMPRESSED_SIZE})"

# ============================================================
# 9. LIMPIEZA DE BACKUPS ANTIGUOS (mantener últimos 5)
# ============================================================
log "🧹 Limpiando backups antiguos (manteniendo últimos 5)..."

cd "$BACKUP_ROOT"
ls -t backup_*.tar.gz 2>/dev/null | tail -n +6 | xargs -r rm -f
ls -td backup_2* 2>/dev/null | tail -n +6 | xargs -r rm -rf

log "✅ Limpieza completada"

# ============================================================
# 10. RESUMEN FINAL
# ============================================================
echo ""
echo "========================================"
echo -e "${GREEN}✅ BACKUP COMPLETADO EXITOSAMENTE${NC}"
echo "========================================"
echo "📁 Ubicación: ${BACKUP_DIR}"
echo "📦 Comprimido: ~/backups/seguimiento360/backup_${TIMESTAMP}.tar.gz"
echo "📊 Tamaño: ${BACKUP_SIZE} (comprimido: ${COMPRESSED_SIZE})"
echo "📄 Archivos: ${FILE_COUNT}"
echo "📋 Log completo: ${LOG_FILE}"
echo "========================================"

