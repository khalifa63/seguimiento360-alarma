# Componente "main" del nodo REED.
# Contrato del frame v1: firmware/shared/espnow_protocol (SPEC-02 v0.3).
idf_component_register(
    SRCS "main.c" "pairing.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer driver nvs_flash espnow_protocol
)
