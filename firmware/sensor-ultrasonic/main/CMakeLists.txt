idf_component_register(
    SRCS "main.c" "pairing.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer driver nvs_flash espnow_protocol
)
