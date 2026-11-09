set(HUB_MAIN_REQUIRES
    bootloader_support console efuse esp_app_format esp_event esp_hosted
    esp_hw_support esp_http_server esp_lcd esp_lcd_st7701
    esp_lcd_touch_gt911 esp_lvgl_port esp_netif esp_partition esp_system
    esp_wifi_remote esp_driver_gpio esp_driver_i2c esp_driver_uart freertos
    log lvgl mdns nvs_flash openthread vfs
)

if(CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE)
    list(APPEND HUB_MAIN_REQUIRES esp-zigbee-lib)
endif()

set(HUB_MAIN_SOURCES
    "main.cpp" "c6_ota.cpp" "c6_zigbee_bridge.cpp" "board_hmi.cpp"
    "wifi_provisioning.cpp" "wifi_wizard.cpp" "diagnostics_http.cpp"
    "multipan_service.cpp" "web_security.cpp" "web_zigbee_approval.cpp"
)

if(CONFIG_HUB_UART_DIAGNOSTIC_ENABLE)
    list(APPEND HUB_MAIN_SOURCES "uart_diagnostic.cpp")
endif()

idf_component_register(
    SRCS ${HUB_MAIN_SOURCES}
    INCLUDE_DIRS "." "../../common"
    REQUIRES ${HUB_MAIN_REQUIRES}
    PRIV_REQUIRES esp_wifi mbedtls
)