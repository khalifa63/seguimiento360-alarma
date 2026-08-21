# CMakeLists.txt del componente main (punto de entrada)
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hub_core nvs_flash
)
