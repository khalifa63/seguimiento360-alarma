idf_component_register(
    SRCS "src/sm_prov.c"
    INCLUDE_DIRS "include"
    REQUIRES hub_core esp_wifi esp_event esp_netif driver freertos web_config
)
