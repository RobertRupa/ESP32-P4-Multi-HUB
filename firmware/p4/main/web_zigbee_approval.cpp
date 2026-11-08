#include "web_zigbee_approval.hpp"

#include "esp_timer.h"

namespace hub::diagnostics {

esp_err_t WebZigbeeApproval::request(WebZigbeeAction action,
                                     std::uint32_t* request_id) noexcept {
    if (request_id == nullptr ||
        (action != WebZigbeeAction::create_network &&
         action != WebZigbeeAction::open_join_window &&
         action != WebZigbeeAction::scan_nearby)) {
        return ESP_ERR_INVALID_ARG;
    }
    const std::int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    if (snapshot_.state == WebZigbeeRequestState::awaiting_touch ||
        snapshot_.state == WebZigbeeRequestState::approved ||
        snapshot_.state == WebZigbeeRequestState::executing) {
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_INVALID_STATE;
    }
    ++snapshot_.id;
    if (snapshot_.id == 0) {
        ++snapshot_.id;
    }
    snapshot_.action = action;
    snapshot_.state = WebZigbeeRequestState::awaiting_touch;
    snapshot_.result = ESP_OK;
    requested_at_us_ = now_us;
    *request_id = snapshot_.id;
    portEXIT_CRITICAL(&mux_);
    return ESP_OK;
}

bool WebZigbeeApproval::read_snapshot(
    WebZigbeeRequestSnapshot* snapshot) noexcept {
    if (snapshot == nullptr) {
        return false;
    }
    const std::int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    if ((snapshot_.state == WebZigbeeRequestState::awaiting_touch ||
         snapshot_.state == WebZigbeeRequestState::approved) &&
        now_us - requested_at_us_ >= kRequestTimeoutUs) {
        snapshot_.state = WebZigbeeRequestState::expired;
        snapshot_.result = ESP_ERR_TIMEOUT;
    }
    *snapshot = snapshot_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

esp_err_t WebZigbeeApproval::approve(std::uint32_t request_id) noexcept {
    const std::int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    if (snapshot_.id != request_id ||
        snapshot_.state != WebZigbeeRequestState::awaiting_touch) {
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_INVALID_STATE;
    }
    if (now_us - requested_at_us_ >= kRequestTimeoutUs) {
        snapshot_.state = WebZigbeeRequestState::expired;
        snapshot_.result = ESP_ERR_TIMEOUT;
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_TIMEOUT;
    }
    snapshot_.state = WebZigbeeRequestState::approved;
    portEXIT_CRITICAL(&mux_);
    return ESP_OK;
}

bool WebZigbeeApproval::claim(std::uint32_t request_id,
                              WebZigbeeAction* action) noexcept {
    if (action == nullptr) {
        return false;
    }
    const std::int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    const bool matches = snapshot_.id == request_id &&
        (snapshot_.state == WebZigbeeRequestState::awaiting_touch ||
         snapshot_.state == WebZigbeeRequestState::approved);
    if (!matches) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    if (now_us - requested_at_us_ >= kRequestTimeoutUs) {
        snapshot_.state = WebZigbeeRequestState::expired;
        snapshot_.result = ESP_ERR_TIMEOUT;
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    *action = snapshot_.action;
    snapshot_.state = WebZigbeeRequestState::executing;
    portEXIT_CRITICAL(&mux_);
    return true;
}

esp_err_t WebZigbeeApproval::complete(std::uint32_t request_id,
                                      esp_err_t result) noexcept {
    portENTER_CRITICAL(&mux_);
    if (snapshot_.id != request_id ||
        snapshot_.state != WebZigbeeRequestState::executing) {
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_INVALID_STATE;
    }
    snapshot_.result = result;
    snapshot_.state = result == ESP_OK ? WebZigbeeRequestState::completed
                                       : WebZigbeeRequestState::failed;
    portEXIT_CRITICAL(&mux_);
    return ESP_OK;
}

esp_err_t WebZigbeeApproval::reject(std::uint32_t request_id) noexcept {
    const std::int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&mux_);
    if (snapshot_.id != request_id ||
        snapshot_.state != WebZigbeeRequestState::awaiting_touch) {
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_INVALID_STATE;
    }
    if (now_us - requested_at_us_ >= kRequestTimeoutUs) {
        snapshot_.state = WebZigbeeRequestState::expired;
        snapshot_.result = ESP_ERR_TIMEOUT;
        portEXIT_CRITICAL(&mux_);
        return ESP_ERR_TIMEOUT;
    }
    snapshot_.state = WebZigbeeRequestState::rejected;
    snapshot_.result = ESP_OK;
    portEXIT_CRITICAL(&mux_);
    return ESP_OK;
}

}
