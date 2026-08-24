idf_component_register(
    SRCS "node_sim_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event esp_timer driver
)
