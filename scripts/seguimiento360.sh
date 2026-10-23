#!/bin/bash
# ============================================================================
# Seguimiento 360 - Script de Automatización Completo
# Autor: Ingeniero de Firmware Senior
# Versión: 1.0 - Fase 4 (Multi-tenant & Portal v2)
# ============================================================================

set -e  # Salir inmediatamente si un comando falla

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Variables de configuración
PROJECT_ROOT="$HOME/seguimiento360-alarma/firmware/hub"
MAIN_FILE="$PROJECT_ROOT/main/main.c"
SERIAL_PORT="/dev/ttyACM0"
BAUD_RATE=115200

# ============================================================================
# FUNCIONES DE UTILIDAD
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

check_directory() {
    if [ ! -d "$PROJECT_ROOT" ]; then
        log_error "Directorio del proyecto no encontrado: $PROJECT_ROOT"
        exit 1
    fi
}

check_serial_port() {
    if [ ! -e "$SERIAL_PORT" ]; then
        log_error "Puerto serial no encontrado: $SERIAL_PORT"
        log_info "Conecta el ESP32-S3 y verifica con: ls /dev/ttyACM*"
        exit 1
    fi
    log_success "Puerto serial encontrado: $SERIAL_PORT"
}

# ============================================================================
# FASE 1: LIMPIEZA DEL PROYECTO
# ============================================================================

cleanup_project() {
    log_info "=== FASE 1: Limpieza del Proyecto ==="
    cd "$PROJECT_ROOT" || exit 1
    
    # Eliminar archivos basura creados por redirecciones incorrectas
    log_info "Eliminando archivos basura..."
    rm -f seq src_mac* 2>/dev/null || true
    
    # Verificar si se eliminaron
    if ls src_mac* 1> /dev/null 2>&1; then
        log_warning "Algunos archivos src_mac* no pudieron eliminarse"
    else
        log_success "Archivos basura eliminados"
    fi
    
    # Limpiar build anterior (opcional, descomenta si quieres clean build)
    # log_info "Limpiando directorio build..."
    # rm -rf build/*
    
    log_success "Limpieza completada"
}

# ============================================================================
# FASE 2: VERIFICACIÓN Y CORRECCIÓN DE ARCHIVOS
# ============================================================================

verify_cmake_files() {
    log_info "=== FASE 2: Verificación de Archivos CMake ==="
    cd "$PROJECT_ROOT" || exit 1
    
    # Verificar CMakeLists.txt raíz
    log_info "Verificando CMakeLists.txt raíz..."
    if ! grep -q "cmake_minimum_required" CMakeLists.txt; then
        log_error "CMakeLists.txt raíz incorrecto. Corrigiendo..."
        cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(seguimiento360_hub)
EOF
        log_success "CMakeLists.txt raíz corregido"
    else
        log_success "CMakeLists.txt raíz válido"
    fi
    
    # Verificar main/CMakeLists.txt
    log_info "Verificando main/CMakeLists.txt..."
    if [ ! -f "main/CMakeLists.txt" ]; then
        log_error "main/CMakeLists.txt no encontrado"
        exit 1
    fi
    log_success "main/CMakeLists.txt existe"
}

fix_sta_netif_error() {
    log_info "=== FASE 2.1: Corrección de Error sta_netif ==="
    
    if [ ! -f "$MAIN_FILE" ]; then
        log_error "Archivo main.c no encontrado: $MAIN_FILE"
        exit 1
    fi
    
    # Verificar si el error existe
    if grep -q "^    sta_netif = esp_netif_create_default_wifi_sta();" "$MAIN_FILE"; then
        log_warning "Error detectado: sta_netif sin declaración de tipo"
        log_info "Aplicando corrección..."
        
        # Crear backup
        cp "$MAIN_FILE" "${MAIN_FILE}.bak"
        log_info "Backup creado: ${MAIN_FILE}.bak"
        
        # Corregir el error
        sed -i 's/^    sta_netif = esp_netif_create_default_wifi_sta();$/    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();/' "$MAIN_FILE"
        
        # Verificar corrección
        if grep -q "esp_netif_t \*sta_netif = esp_netif_create_default_wifi_sta();" "$MAIN_FILE"; then
            log_success "Error corregido exitosamente"
        else
            log_error "La corrección no se aplicó correctamente"
            exit 1
        fi
    else
        log_success "No se detectó el error sta_netif (ya está corregido o no existe)"
    fi
}

# ============================================================================
# FASE 3: COMPILACIÓN Y FLASHEO
# ============================================================================

build_firmware() {
    log_info "=== FASE 3: Compilación del Firmware ==="
    cd "$PROJECT_ROOT" || exit 1
    
    # Verificar que IDF_PATH esté configurado
    if [ -z "$IDF_PATH" ]; then
        log_error "IDF_PATH no está configurado"
        log_info "Ejecuta: source \$HOME/esp/esp-idf/export.sh"
        exit 1
    fi
    
    # Configurar target (ESP32-S3)
    log_info "Configurando target: esp32s3"
    idf.py set-target esp32s3
    
    # Compilar
    log_info "Compilando firmware..."
    idf.py build
    
    if [ $? -eq 0 ]; then
        log_success "Compilación exitosa"
    else
        log_error "Error en la compilación"
        exit 1
    fi
}

flash_firmware() {
    log_info "=== FASE 4: Flasheo del Firmware ==="
    check_serial_port
    
    cd "$PROJECT_ROOT" || exit 1
    
    log_info "Flasheando firmware en $SERIAL_PORT..."
    idf.py -p "$SERIAL_PORT" flash
    
    if [ $? -eq 0 ]; then
        log_success "Flasheo completado exitosamente"
    else
        log_error "Error en el flasheo"
        exit 1
    fi
}

monitor_firmware() {
    log_info "=== FASE 5: Monitor del Firmware ==="
    check_serial_port
    
    cd "$PROJECT_ROOT" || exit 1
    
    log_info "Iniciando monitor (Ctrl+] para salir)..."
    idf.py -p "$SERIAL_PORT" monitor
}

# ============================================================================
# FASE 6: COMANDOS ÚTILES DEL SERVIDOR DOCKER
# ============================================================================

server_status() {
    log_info "=== Estado del Servidor Docker ==="
    cd "$HOME/seguimiento360-alarma/server" || exit 1
    
    docker-compose ps
}

server_logs() {
    log_info "=== Logs del Servidor Docker ==="
    cd "$HOME/seguimiento360-alarma/server" || exit 1
    
    if [ -z "$1" ]; then
        docker-compose logs -f
    else
        docker-compose logs -f "$1"
    fi
}

server_restart() {
    log_info "=== Reiniciando Servidor Docker ==="
    cd "$HOME/seguimiento360-alarma/server" || exit 1
    
    docker-compose restart
    log_success "Servidor reiniciado"
}

server_stop() {
    log_info "=== Deteniendo Servidor Docker ==="
    cd "$HOME/seguimiento360-alarma/server" || exit 1
    
    docker-compose stop
    log_success "Servidor detenido"
}

server_start() {
    log_info "=== Iniciando Servidor Docker ==="
    cd "$HOME/seguimiento360-alarma/server" || exit 1
    
    docker-compose up -d
    log_success "Servidor iniciado"
}

# ============================================================================
# MENÚ PRINCIPAL
# ============================================================================

show_menu() {
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║     Seguimiento 360 - Script de Automatización            ║"
    echo "║     Fase 4: Multi-tenant & Portal Cautivo v2              ║"
    echo "╠════════════════════════════════════════════════════════════╣"
    echo "║  FIRMWARE HUB (ESP32-S3)                                  ║"
    echo "║  1) Limpieza completa del proyecto                        ║"
    echo "║  2) Verificar y corregir archivos CMake                   ║"
    echo "║  3) Corregir error sta_netif en main.c                    ║"
    echo "║  4) Compilar firmware                                     ║"
    echo "║  5) Flashear firmware                                     ║"
    echo "║  6) Monitor serial                                        ║"
    echo "║  7) Flujo completo: limpiar → corregir → compilar → flash ║"
    echo "╠════════════════════════════════════════════════════════════╣"
    echo "║  SERVIDOR DOCKER                                          ║"
    echo "║  8) Ver estado del servidor                               ║"
    echo "║  9) Ver logs del servidor                                 ║"
    echo "║  10) Reiniciar servidor                                   ║"
    echo "║  11) Detener servidor                                     ║"
    echo "║  12) Iniciar servidor                                     ║"
    echo "╠════════════════════════════════════════════════════════════╣"
    echo "║  0) Salir                                                 ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""
}

main() {
    while true; do
        show_menu
        read -p "Selecciona una opción [0-12]: " option
        
        case $option in
            1)
                cleanup_project
                ;;
            2)
                verify_cmake_files
                ;;
            3)
                fix_sta_netif_error
                ;;
            4)
                build_firmware
                ;;
            5)
                flash_firmware
                ;;
            6)
                monitor_firmware
                ;;
            7)
                log_info "=== Ejecutando flujo completo ==="
                cleanup_project
                verify_cmake_files
                fix_sta_netif_error
                build_firmware
                flash_firmware
                log_success "Flujo completo finalizado exitosamente"
                ;;
            8)
                server_status
                ;;
            9)
                read -p "Servicio específico (mosquitto/nodered/influxdb/grafana/gotify) o Enter para todos: " service
                server_logs "$service"
                ;;
            10)
                server_restart
                ;;
            11)
                server_stop
                ;;
            12)
                server_start
                ;;
            0)
                log_info "Saliendo..."
                exit 0
                ;;
            *)
                log_error "Opción no válida"
                ;;
        esac
        
        echo ""
        read -p "Presiona Enter para continuar..."
    done
}

# ============================================================================
# MODO NO-INTERACTIVO (para scripts automatizados)
# ============================================================================

if [ "$1" == "--auto" ]; then
    log_info "=== Modo Automático ==="
    cleanup_project
    verify_cmake_files
    fix_sta_netif_error
    build_firmware
    flash_firmware
    log_success "Proceso automático completado"
    exit 0
fi

# Ejecutar menú interactivo
main
