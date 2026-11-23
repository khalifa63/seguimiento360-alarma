cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32cam-evidence)

# Componentes locales
set(COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/main ${CMAKE_CURRENT_LIST_DIR}/components)

# Requeridos por ESP-IDF
idf_component_register(
  SRCS "main/main.c"
  INCLUDE_DIRS "main"
  REQUIRES esp_http_client esp_https_ota esp_timer json wifi esp_camera sdmmc driver
)

# Configuración por defecto (sobreescritible via menuconfig)
# CONFIG_ESPTOOLPY_MONITOR_BAUD=115200