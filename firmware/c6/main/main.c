#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE
#include "c6_zigbee_sdio.h"
#endif

static const char *TAG = "hub_c6_cp";

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s; refusing to erase persistent storage",
                 esp_err_to_name(result));
        return;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Default event loop init failed: %s", esp_err_to_name(result));
        return;
    }

#if CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE
    result = c6_zigbee_sdio_start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "C6 Zigbee SDIO bridge failed to start: %s",
                 esp_err_to_name(result));
        return;
    }
#endif

    ESP_LOGI(TAG, "ESP-Hosted C6 co-processor application started");
}