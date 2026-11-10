#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zigbee_level_state.hpp"
#include "zigbee_on_off_state.hpp"

namespace hub::radio {

inline constexpr std::size_t kMaxZigbeeShortAddresses = 16;
inline constexpr std::size_t kMaxZigbeeEndpointsPerDevice = 4;
inline constexpr std::size_t kMaxZigbeeClustersPerEndpoint = 8;
inline constexpr std::uint16_t kZigbeeOnOffClusterId = 0x0006;
inline constexpr std::uint16_t kZigbeeLevelControlClusterId = 0x0008;

struct ZigbeeEndpointCapabilities {
    std::uint8_t endpoint_id = 0;
    std::uint16_t profile_id = 0;
    std::uint16_t device_id = 0;
    std::uint8_t input_cluster_count = 0;
    std::uint8_t output_cluster_count = 0;
    std::array<std::uint16_t, kMaxZigbeeClustersPerEndpoint>
        input_clusters{};
    std::array<std::uint16_t, kMaxZigbeeClustersPerEndpoint>
        output_clusters{};
    bool clusters_truncated = false;
    ZigbeeOnOffState on_off_state{};
    ZigbeeLevelState level_state{};
};

struct ZigbeeDeviceCapabilities {
    std::uint16_t short_address = 0;
    std::uint8_t endpoint_count = 0;
    bool interview_complete = false;
    bool interview_pending = false;
    bool interview_failed = false;
    esp_err_t interview_error = ESP_OK;
    std::array<ZigbeeEndpointCapabilities, kMaxZigbeeEndpointsPerDevice>
        endpoints{};
};

enum class ZigbeeDeviceCommandKind : std::uint8_t {
    set_on_off,
    set_level,
};

struct ZigbeeDeviceCommand {
    std::uint16_t short_address = 0;
    std::uint8_t endpoint_id = 0;
    ZigbeeDeviceCommandKind kind = ZigbeeDeviceCommandKind::set_on_off;
    std::uint8_t value = 0;
};

struct ZigbeeCoordinatorSnapshot {
    bool available = false;
    bool stack_ready = false;
    bool network_formed = false;
    bool search_pending = false;
    bool formation_timed_out = false;
    std::uint16_t device_count = 0;
    std::array<std::uint16_t, kMaxZigbeeShortAddresses> short_addresses{};
    std::uint8_t short_address_count = 0;
    bool short_address_details_available = false;
    bool short_addresses_truncated = false;
    bool device_capabilities_available = false;
    bool device_capabilities_truncated = false;
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
    virtual esp_err_t request_device_interview(
        std::uint16_t short_address) noexcept {
        (void)short_address;
        return ESP_ERR_NOT_SUPPORTED;
    }
    virtual esp_err_t request_device_on_off_read(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
        (void)short_address;
        (void)endpoint_id;
        return ESP_ERR_NOT_SUPPORTED;
    }
    virtual esp_err_t request_device_level_read(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
        (void)short_address;
        (void)endpoint_id;
        return ESP_ERR_NOT_SUPPORTED;
    }
    virtual bool read_device_capabilities(
        std::uint16_t short_address,
        ZigbeeDeviceCapabilities* capabilities) const noexcept {
        (void)short_address;
        (void)capabilities;
        return false;
    }
    virtual esp_err_t request_device_command(
        const ZigbeeDeviceCommand& command) noexcept {
        (void)command;
        return ESP_ERR_NOT_SUPPORTED;
    }
};

}