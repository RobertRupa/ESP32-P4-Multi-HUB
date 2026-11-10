#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "zigbee_coordinator.hpp"

namespace hub::radio {

using C6ZigbeeSnapshot = ZigbeeCoordinatorSnapshot;

class C6ZigbeeBridge final : public IZigbeeCoordinator {
public:
    C6ZigbeeBridge() = default;
    C6ZigbeeBridge(const C6ZigbeeBridge&) = delete;
    C6ZigbeeBridge& operator=(const C6ZigbeeBridge&) = delete;

    esp_err_t start() noexcept override;
    esp_err_t request_network_start() noexcept override;
    esp_err_t request_pairing_window(
        std::uint8_t duration_seconds) noexcept override;
    esp_err_t request_search() noexcept override;
    bool read_snapshot(
        ZigbeeCoordinatorSnapshot* snapshot) const noexcept override;

private:
    static void peer_data_callback(std::uint32_t message_id,
                                   const std::uint8_t* data,
                                   std::size_t length,
                                   void* context);
    static void command_task(void* context);
    esp_err_t submit_command(std::uint8_t command,
                             std::uint8_t duration_seconds) noexcept;
    esp_err_t send_command(std::uint8_t command,
                           std::uint8_t duration_seconds) noexcept;
    esp_err_t persist_audit(const char* count_key,
                            const char* time_key) noexcept;

    SemaphoreHandle_t snapshot_mutex_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    TaskHandle_t command_task_ = nullptr;
    C6ZigbeeSnapshot snapshot_{};
    bool started_ = false;
};

esp_err_t register_c6_zigbee_console(C6ZigbeeBridge& bridge) noexcept;

}