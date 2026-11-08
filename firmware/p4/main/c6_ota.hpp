#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace hub {

enum class C6OtaAuditState : std::uint8_t {
    unknown,
    started,
    pending_verify,
    verified,
    failed,
};

struct C6DiagnosticsSnapshot {
    bool ota_in_progress = false;
    bool version_valid = false;
    std::uint32_t version_major = 0;
    std::uint32_t version_minor = 0;
    std::uint32_t version_patch = 0;
    esp_err_t version_error = ESP_OK;
    bool cp_info_valid = false;
    std::uint32_t chip_id = 0;
    std::array<char, 32> target{};
    esp_err_t cp_info_error = ESP_OK;
    bool staging_partition_found = false;
    std::uint32_t staging_partition_bytes = 0;
    bool staged_image_valid = false;
    std::uint32_t staged_image_bytes = 0;
    bool staged_image_hash_valid = false;
    std::array<std::uint8_t, 32> staged_image_sha256{};
    std::array<char, 32> staged_app_version{};
    esp_err_t staged_image_error = ESP_OK;
    C6OtaAuditState audit_state = C6OtaAuditState::unknown;
    std::uint32_t ota_attempts = 0;
    std::int32_t ota_last_error = ESP_OK;
    bool audit_available = false;
};

esp_err_t register_c6_ota_command() noexcept;
esp_err_t register_c6_diagnostics_command() noexcept;
esp_err_t read_c6_diagnostics(C6DiagnosticsSnapshot* snapshot) noexcept;
void verify_c6_ota_after_boot(std::uint32_t major,
                              std::uint32_t minor,
                              std::uint32_t patch) noexcept;

}