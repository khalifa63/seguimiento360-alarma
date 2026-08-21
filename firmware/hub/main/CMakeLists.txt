idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hub_core espnow_link mqtt_link sm_conn
             nvs_flash json esp_wifi esp_netif esp_timer
             esp-tls mbedtls esp_event
)
