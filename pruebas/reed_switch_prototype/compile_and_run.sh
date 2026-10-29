#!/bin/bash
# Script de compilación y carga del prototipo Reed Switch (ESP32 DevKit V1).
# Uso:  ./compile_and_run.sh
# Compila el firmware y, si detecta la placa, la flashea y abre el monitor.
# Requisitos: ESP-IDF v5.x instalado (ver README.md, "Preparar el entorno en Linux").

echo "=========================================="
echo "COMPILACIÓN REED SWITCH: $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "=========================================="

# 1. Configurar entorno ESP-IDF (si no está ya activo)
if [ -z "$IDF_PATH" ]; then
    echo "[1/5] Configurando entorno ESP-IDF..."
    if [ -f "$HOME/esp-idf/export.sh" ]; then
        # shellcheck disable=SC1091
        . "$HOME/esp-idf/export.sh" >/dev/null 2>&1
    elif [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        # shellcheck disable=SC1091
        . "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1
    elif [ -f "$HOME/.espressif/esp-idf/export.sh" ]; then
        # shellcheck disable=SC1091
        . "$HOME/.espressif/esp-idf/export.sh" >/dev/null 2>&1
    else
        echo "  ✗ Error: ESP-IDF no encontrado."
        echo "  Instálalo según el README.md (sección 'Preparar el entorno en Linux')."
        exit 1
    fi
fi
echo "[1/5] ✓ ESP-IDF: $IDF_PATH"

# 2. Ir al directorio del proyecto
cd "$(dirname "$0")" || exit 1
echo "[2/5] ✓ Directorio: $(pwd)"

# 3. Verificar estructura mínima del proyecto
if [ ! -f "CMakeLists.txt" ] || [ ! -f "main/CMakeLists.txt" ] || [ ! -f "main/main.c" ]; then
    echo "  ✗ Error: falta CMakeLists.txt, main/CMakeLists.txt o main/main.c"
    exit 1
fi
echo "[3/5] ✓ Estructura verificada (CMakeLists.txt, main/main.c, main/CMakeLists.txt)"

# 4. Compilar (target ESP32 clásico del DevKit V1)
echo "[4/5] Compilando (idf.py set-target esp32 && idf.py build)..."
idf.py set-target esp32 > build.log 2>&1
if [ $? -ne 0 ]; then
    echo "  ✗ Error en set-target. Últimas líneas del log:"
    tail -20 build.log
    exit 1
fi
idf.py build >> build.log 2>&1
if [ $? -eq 0 ]; then
    echo "  ✓ Compilación exitosa: build/reed_switch_prototype.bin ($(du -h build/reed_switch_prototype.bin 2>/dev/null | cut -f1))"
else
    echo "  ✗ Error de compilación. Últimas líneas del log:"
    tail -30 build.log
    exit 1
fi

# 5. Detectar puerto serial, flashear y abrir monitor
echo "[5/5] Detección de puerto serial..."
PORT=""
for p in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
    if [ -c "$p" ]; then
        PORT="$p"
        break
    fi
done

if [ -z "$PORT" ]; then
    echo "  ⚠ No se detectó puerto serial. Conecta el ESP32 y ejecuta manualmente:"
    echo "    idf.py -p /dev/ttyUSB0 flash monitor"
    exit 0
fi

echo "  → Puerto detectado: $PORT"
echo "  → Flasheando y abriendo monitor (salir con Ctrl+])..."
idf.py -p "$PORT" flash monitor
