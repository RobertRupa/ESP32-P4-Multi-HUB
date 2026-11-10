set(HUB_C6_SOURCES "main.c")
set(HUB_C6_PRIV_REQUIRES esp_event log nvs_flash)

if(CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE)
    list(APPEND HUB_C6_SOURCES "c6_zigbee_sdio.c")
    list(APPEND HUB_C6_PRIV_REQUIRES esp_coex esp_driver_uart esp-zigbee-lib openthread vfs)
endif()

idf_component_register(
    SRCS ${HUB_C6_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_hosted
    PRIV_REQUIRES ${HUB_C6_PRIV_REQUIRES}
)