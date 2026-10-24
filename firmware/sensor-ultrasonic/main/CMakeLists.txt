idf_component_register(
    SRCS "main.c" "pairing.c" "ota_node.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer driver nvs_flash espnow_protocol esp_netif esp_event esp_http_client esp_https_ota app_update mbedtls
)
