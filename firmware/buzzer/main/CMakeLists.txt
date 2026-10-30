# Componente "main" del nodo BUZZER (sirena).
# Contrato del frame v1 y payload de activacion: firmware/shared/espnow_protocol
# (SPEC-02 v0.4, HUB-IF-015).
idf_component_register(
    SRCS "main.c" "pairing.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer driver nvs_flash espnow_protocol
)
