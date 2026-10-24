idf_component_register(
    SRCS "main.c" "ota_manager.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_https_ota app_update
    REQUIRES hub_core espnow_link espnow_protocol mqtt_link sm_conn sm_alarm sm_prov web_config
             nvs_flash json esp_wifi esp_netif esp_timer driver
)
