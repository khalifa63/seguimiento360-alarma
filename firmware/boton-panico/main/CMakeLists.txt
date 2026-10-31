# Componente "main" del nodo BOTON DE PANICO (pulsador).
# Contrato del frame v1 y del evento PANIC: firmware/shared/espnow_protocol
# (SPEC-02 v0.6, HUB-IF-002/005/015).
idf_component_register(
    SRCS "main.c" "pairing.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer driver nvs_flash espnow_protocol
)
