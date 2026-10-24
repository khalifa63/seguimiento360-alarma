cmake_minimum_required(VERSION 3.16)

# El hub comparte el contrato del frame v1 con los nodos.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Version explicita. Sin esto, ESP-IDF usa `git describe`, que produce
# cadenas como "v6.0.0-pre-ota-59-gbb71bbb-dirt": imposibles de comparar
# para decidir si un hub necesita actualizacion. Se toma del sdkconfig,
# que ya declara 6.1.0 para la linea OTA.
set(PROJECT_VER "6.1.0")
project(seguimiento360_hub)
