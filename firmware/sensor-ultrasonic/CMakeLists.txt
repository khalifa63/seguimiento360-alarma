cmake_minimum_required(VERSION 3.16)

# El nodo comparte el contrato del frame v1 con el hub.
# shared/ contiene los componentes comunes a ambos proyectos.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sensor_ultrasonic)
