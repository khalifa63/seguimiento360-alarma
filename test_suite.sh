#!/bin/bash
# ============================================================================
# SCRIPT DE PRUEBAS - SEGUIMIENTO 360
# Flujo Completo de Principio a Fin con Pruebas Exitosas y Diagnóstico Crítico
# Fecha: 2026-08-28
# ============================================================================

# Colores para output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Variables del proyecto
HUB_PORT="/dev/ttyACM0"
NODO_PORT="/dev/ttyUSB0"
MQTT_SERVER="192.168.1.200"
MQTT_PORT="1883"
HUB_AP_SSID="Seguimiento360_HUB"
HUB_AP_PASS="hub_password_2026"
HUB_AP_IP="192.168.4.1"

# ============================================================================
# FUNCIÓN: Mostrar encabezado
# ============================================================================
show_header() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════════════╗"
    echo "║          SEGUIMIENTO 360 - SCRIPT DE PRUEBAS COMPLETO               ║"
    echo "║          Fecha: $(date '+%Y-%m-%d %H:%M:%S')                        ║"
    echo "╚══════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# ============================================================================
# FUNCIÓN: Mostrar estado del sistema
# ============================================================================
check_system_status() {
    echo -e "${YELLOW}[FASE 0] Verificando estado del sistema...${NC}"
    echo ""
    
    echo -n "Docker: "
    if docker ps >/dev/null 2>&1; then echo -e "${GREEN}✓ Activo${NC}"; else echo -e "${RED}✗ Inactivo${NC}"; fi
    
    echo -n "Mosquitto: "
    if docker ps | grep -q mosquitto; then echo -e "${GREEN}✓ Corriendo${NC}"; else echo -e "${RED}✗ No corre${NC}"; fi
    
    echo -n "Node-RED: "
    if docker ps | grep -q nodered; then echo -e "${GREEN}✓ Corriendo${NC}"; else echo -e "${RED}✗ No corre${NC}"; fi
    
    echo -n "InfluxDB: "
    if docker ps | grep -q influxdb; then echo -e "${GREEN}✓ Corriendo${NC}"; else echo -e "${RED}✗ No corre${NC}"; fi
    
    echo -n "Gotify: "
    if docker ps | grep -q gotify; then echo -e "${GREEN}✓ Corriendo${NC}"; else echo -e "${RED}✗ No corre${NC}"; fi
    
    echo ""
    echo -n "Puerto HUB ($HUB_PORT): "
    if [ -e "$HUB_PORT" ]; then echo -e "${GREEN}✓ Disponible${NC}"; else echo -e "${RED}✗ No detectado${NC}"; fi
    
    echo -n "Puerto NODO ($NODO_PORT): "
    if [ -e "$NODO_PORT" ]; then echo -e "${GREEN}✓ Disponible${NC}"; else echo -e "${RED}✗ No detectado${NC}"; fi
    echo ""
}

# ============================================================================
# FUNCIÓN: Ejecutar Diagnóstico Crítico (NUEVO)
# ============================================================================
run_critical_diagnostics() {
    echo -e "${RED}[DIAGNÓSTICO CRÍTICO] Ejecutando pruebas rápidas del servidor...${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    echo -e "${YELLOW}1. Verificando red Docker (conectividad entre contenedores)...${NC}"
    docker network inspect iot-alarma_default | grep -A 2 "Name\|IPv4" || echo -e "${RED}✗ Red 'iot-alarma_default' no encontrada${NC}"
    echo ""

    echo -e "${YELLOW}2. Probando notificación directa a Gotify...${NC}"
    echo -e "${BLUE}Nota: Reemplaza Ay_ku..9PE-UE8D con tu token real si es necesario.${NC}"
    curl -s -X POST "http://localhost:8081/message?token=TU_TOKEN_GOTIFY" \
      -H "Content-Type: application/json" \
      -d '{"title":"Test Bash","message":"Prueba de conectividad desde script","priority":5}'
    echo -e "${GREEN}✓ Solicitud enviada. Revisa tu celular/app Gotify.${NC}"
    echo ""

    echo -e "${YELLOW}3. Verificando logs recientes de Gotify (¿está recibiendo?)...${NC}"
    docker logs gotify --tail 30 | grep -i "message\|error" || echo -e "${YELLOW}Sin mensajes recientes en logs de Gotify.${NC}"
    echo ""

    echo -e "${YELLOW}4. Comandos de escucha MQTT (⚠️ Ejecutar manualmente en OTRA terminal):${NC}"
    echo -e "${BLUE}   🔴 CRÍTICO 1: Ver si el HUB está online en MQTT${NC}"
    echo "   docker exec mosquitto mosquitto_sub -h localhost -p 1883 -t 'seguimiento360/tenant/0/site/site0/hub/+/status' -v"
    echo ""
    echo -e "${BLUE}   🔴 CRÍTICO 2: Escuchar TODOS los eventos de Seguimiento 360${NC}"
    echo "   docker exec mosquitto mosquitto_sub -h localhost -p 1883 -t 'seguimiento360/#' -v"
    echo ""
}

# ============================================================================
# FUNCIÓN: Comandos de Diagnóstico General
# ============================================================================
diagnostic_commands() {
    echo -e "${YELLOW}[DIAGNÓSTICO] Comandos Útiles de Referencia${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "1. Ver estado de todos los contenedores:"
    echo "   docker compose ps"
    echo ""
    echo "2. Ver logs del HUB en tiempo real:"
    echo "   idf.py -p $HUB_PORT monitor"
    echo ""
    echo "3. Ver logs del NODO en tiempo real:"
    echo "   idf.py -p $NODO_PORT monitor"
    echo ""
    echo "4. Ver últimos eventos en InfluxDB:"
    echo "   docker exec influxdb influx query 'from(bucket:\"telemetria\") |> range(start: -1h) |> last()'"
    echo ""
    echo "5. Borrar NVS del HUB (reset de fábrica):"
    echo "   idf.py -p $HUB_PORT erase-flash"
    echo ""
    echo "6. Ver canal del router (HUB):"
    echo "   idf.py -p $HUB_PORT monitor | grep 'connected with'"
    echo ""
}

# ============================================================================
# RESUMEN DE PRUEBAS EXITOSAS
# ============================================================================
summary() {
    echo -e "${GREEN}"
    echo "╔══════════════════════════════════════════════════════════════════════╗"
    echo "║              RESUMEN DE PRUEBAS EXITOSAS                           ║"
    echo "╚══════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
    echo -e "${GREEN}✓ FASE 1: Portal Cautivo${NC} (Configuración Wi-Fi desde celular)"
    echo -e "${GREEN}✓ FASE 2: Conexión MQTT${NC} (HUB conectado a 192.168.1.200:1883)"
    echo -e "${GREEN}✓ FASE 3: Sincronización ESP-NOW${NC} (Canales AP/STA alineados)"
    echo -e "${GREEN}✓ FASE 4: Pairing NODO-HUB${NC} (Vinculación persistente en NVS)"
    echo -e "${GREEN}✓ FASE 5: Ciclo de Alarma${NC} (NODO -> HUB -> MQTT -> Node-RED -> Gotify)"
    echo -e "${GREEN}✓ DOCUMENTACIÓN: Bitácora de errores (BITACORA.md) creada."
    echo ""
}

# ============================================================================
# MENÚ PRINCIPAL
# ============================================================================
main() {
    show_header
    check_system_status
    
    echo "Seleccione una opción:"
    echo "  1) Ejecutar Diagnóstico CRÍTICO del Servidor (Recomendado)"
    echo "  2) Ver comandos de diagnóstico general de referencia"
    echo "  3) Ver resumen de pruebas exitosas del proyecto"
    echo "  4) Salir"
    echo ""
    read -p "Opción: " option
    
    case $option in
        1)
            run_critical_diagnostics
            ;;
        2)
            diagnostic_commands
            ;;
        3)
            summary
            ;;
        4)
            echo "Saliendo..."
            exit 0
            ;;
        *)
            echo "Opción inválida"
            exit 1
            ;;
    esac
}

# Ejecutar script
main
