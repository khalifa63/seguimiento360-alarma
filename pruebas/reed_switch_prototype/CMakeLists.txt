cmake_minimum_required(VERSION 3.16)

# Prototipo: Reed Switch magnético de 28 mm con ESP32 DevKit V1.
# Proyecto standalone de pruebas: solo usa componentes estándar de ESP-IDF
# (no depende de firmware/shared).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reed_switch_prototype)
