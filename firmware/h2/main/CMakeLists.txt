idf_component_register(
    SRCS "main.c" "uart_diagnostic.c"
    INCLUDE_DIRS "." "../../common"
    REQUIRES esp_driver_gpio esp_driver_uart esp_event freertos log openthread nvs_flash vfs
)