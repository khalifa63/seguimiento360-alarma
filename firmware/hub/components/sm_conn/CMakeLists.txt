idf_component_register(
    SRCS "src/sm_conn.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif mqtt_link
)
