cmake_minimum_required(VERSION 3.16)

# El nodo comparte el contrato del frame v1 con el hub (firmware/shared).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reed_sensor)
