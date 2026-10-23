cmake_minimum_required(VERSION 3.16)

# El hub comparte el contrato del frame v1 con los nodos.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(seguimiento360_hub)
