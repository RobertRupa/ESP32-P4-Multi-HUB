#pragma once

#include <cstdint>

#include "esp_err.h"

namespace hub::radio {

struct ZigbeeCoordinatorSnapshot {
    bool available = false;
    bool stack_ready = false;
    bool network_formed = false;
    bool search_pending = false;
    bool formation_timed_out = false;
    std::uint16_t device_count = 0;
    std::uint8_t pairing_seconds_remaining = 0;
    esp_err_t last_error = ESP_OK;
    std::uint16_t last_signal_type = 0;
    std::uint8_t last_bdb_status = 0;
    std::int32_t last_bdb_result = 0;
    std::uint8_t last_bdb_stage = 0;
};

class IZigbeeCoordinator {
public:
    virtual ~IZigbeeCoordinator() = default;

    virtual esp_err_t start() noexcept = 0;
    virtual esp_err_t request_network_start() noexcept = 0;
    virtual esp_err_t request_pairing_window(
        std::uint8_t duration_seconds) noexcept = 0;
    virtual esp_err_t request_search() noexcept = 0;
    virtual bool read_snapshot(
        ZigbeeCoordinatorSnapshot* snapshot) const noexcept = 0;
};

}