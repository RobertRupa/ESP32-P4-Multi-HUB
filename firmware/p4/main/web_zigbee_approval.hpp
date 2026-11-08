#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace hub::diagnostics {

enum class WebZigbeeAction : std::uint8_t {
    none,
    create_network,
    open_join_window,
    scan_nearby,
};

enum class WebZigbeeRequestState : std::uint8_t {
    idle,
    awaiting_touch,
    approved,
    executing,
    completed,
    rejected,
    expired,
    failed,
};

struct WebZigbeeRequestSnapshot {
    std::uint32_t id = 0;
    WebZigbeeAction action = WebZigbeeAction::none;
    WebZigbeeRequestState state = WebZigbeeRequestState::idle;
    esp_err_t result = ESP_OK;
};

class WebZigbeeApproval final {
public:
    esp_err_t request(WebZigbeeAction action, std::uint32_t* request_id) noexcept;
    bool read_snapshot(WebZigbeeRequestSnapshot* snapshot) noexcept;
    esp_err_t approve(std::uint32_t request_id) noexcept;
    bool claim(std::uint32_t request_id, WebZigbeeAction* action) noexcept;
    esp_err_t complete(std::uint32_t request_id, esp_err_t result) noexcept;
    esp_err_t reject(std::uint32_t request_id) noexcept;

private:
    static constexpr std::int64_t kRequestTimeoutUs = 60000000;

    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    WebZigbeeRequestSnapshot snapshot_{};
    std::int64_t requested_at_us_ = 0;
};

}
