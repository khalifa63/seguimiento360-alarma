idf_component_register(
    SRCS "src/sm_alarm.c"
    INCLUDE_DIRS "include"
    REQUIRES hub_core esp_timer json
)
