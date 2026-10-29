# Componente "main" del prototipo Reed Switch.
# "driver" aporta driver/gpio.h; freertos y log son requisitos comunes de
# ESP-IDF y no hace falta declararlos.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
