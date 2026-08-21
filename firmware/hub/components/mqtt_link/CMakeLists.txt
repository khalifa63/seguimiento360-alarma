idf_component_register(
    SRCS "src/mqtt_link.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt json esp_event esp_timer
)
