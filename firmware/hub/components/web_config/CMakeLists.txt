idf_component_register(
    SRCS "src/web_config.c" "src/dns_server.c"
    INCLUDE_DIRS "include" "src"
    REQUIRES hub_core sm_prov esp_http_server esp_wifi esp_netif json lwip
)
