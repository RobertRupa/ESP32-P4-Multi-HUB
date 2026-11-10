#include "c6_ota.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "nvs.h"

namespace hub {
namespace {

constexpr char kTag[] = "c6_ota";
constexpr char kPartitionLabel[] = "c6_fw";
constexpr char kExpectedProject[] = "hub_c6_esp_hosted_coprocessor";
constexpr char kNvsNamespace[] = "hub_c6_ota";
constexpr char kConfirmation[] = "CONFIRM-C6-ESP-HOSTED-3.1.0";
constexpr char kMultipanConfirmation[] = "CONFIRM-C6-MULTIPAN-69F6E8C7D6E6CF00";
constexpr char kSdioZigbeeConfirmation[] = "CONFIRM-C6-ZIGBEE-SDIO-55A9CA31465B2C62";
constexpr char kSdioZigbeeBdbTimeoutConfirmation[] =
    "CONFIRM-C6-ZIGBEE-SDIO-D081A49ADAAB719A";
constexpr char kSdioZigbeeBdbTraceConfirmation[] =
    "CONFIRM-C6-ZIGBEE-LOGS-3403B461EFBB0873";
constexpr char kSdioZigbeeBdbV3Confirmation[] =
    "CONFIRM-C6-ZIGBEE-BDB-V3-133B8B85130D04C1";
constexpr char kH2WifiOnlyConfirmation[] =
    "CONFIRM-C6-H2-WIFI-7C7A58CA92276F35";
constexpr std::array<std::uint8_t, 32> kMultipanImageSha256 = {
    0x69, 0xF6, 0xE8, 0xC7, 0xD6, 0xE6, 0xCF, 0x00,
    0x39, 0x86, 0x14, 0x8F, 0xBC, 0xEA, 0xD6, 0x46,
    0x26, 0x54, 0xA9, 0xDE, 0x4F, 0xBB, 0x74, 0xDB,
    0xF3, 0x55, 0xA9, 0x99, 0x61, 0x44, 0x10, 0xF4,
};
constexpr std::array<std::uint8_t, 32> kSdioZigbeeImageSha256 = {
    0x55, 0xA9, 0xCA, 0x31, 0x46, 0x5B, 0x2C, 0x62,
    0xAA, 0x73, 0xAD, 0xBB, 0x61, 0x0C, 0xDA, 0xC2,
    0x95, 0x69, 0x88, 0x6E, 0x8D, 0xFC, 0x43, 0xB7,
    0xEA, 0x0B, 0xF4, 0x1A, 0xAE, 0x31, 0xB6, 0x9E,
};
constexpr std::array<std::uint8_t, 32> kSdioZigbeeBdbTimeoutImageSha256 = {
    0xD0, 0x81, 0xA4, 0x9A, 0xDA, 0xAB, 0x71, 0x9A,
    0x7F, 0x96, 0x35, 0xB7, 0x14, 0xA6, 0x27, 0x87,
    0x6D, 0xF2, 0xBE, 0xD9, 0x2B, 0x21, 0xA0, 0x49,
    0x42, 0xBD, 0x08, 0x6E, 0xE0, 0xD4, 0x75, 0x30,
};
constexpr std::array<std::uint8_t, 32> kSdioZigbeeBdbTraceImageSha256 = {
    0x34, 0x03, 0xB4, 0x61, 0xEF, 0xBB, 0x08, 0x73,
    0x56, 0x93, 0x75, 0x06, 0x56, 0x84, 0xE5, 0xDA,
    0x17, 0x18, 0x4B, 0xB5, 0x04, 0x4E, 0xDD, 0x50,
    0x70, 0x43, 0xB5, 0x38, 0xFD, 0x6B, 0x42, 0x47,
};
constexpr std::array<std::uint8_t, 32> kSdioZigbeeBdbV3ImageSha256 = {
    0x13, 0x3B, 0x8B, 0x85, 0x13, 0x0D, 0x04, 0xC1,
    0x52, 0x92, 0x68, 0xE1, 0xD4, 0xD9, 0x49, 0x96,
    0xAF, 0x42, 0xDA, 0x4A, 0x2E, 0x25, 0x69, 0x8C,
    0x6A, 0x88, 0x15, 0xAA, 0xA5, 0xC1, 0x2D, 0xAF,
};
constexpr std::array<std::uint8_t, 32> kH2WifiOnlyImageSha256 = {
    0x7C, 0x7A, 0x58, 0xCA, 0x92, 0x27, 0x6F, 0x35,
    0x2F, 0x79, 0x98, 0xF1, 0x41, 0xDB, 0x0C, 0x44,
    0x2B, 0x18, 0x45, 0x18, 0xDE, 0x0A, 0xEE, 0x8A,
    0xBA, 0x04, 0x7C, 0x84, 0x81, 0xD4, 0x59, 0xB9,
};
constexpr std::uint32_t kTargetMajor = 3;
constexpr std::uint32_t kTargetMinor = 1;
constexpr std::uint32_t kTargetPatch = 0;
constexpr std::uint8_t kStateStarted = 1;
constexpr std::uint8_t kStatePendingVerify = 2;
constexpr std::uint8_t kStateVerified = 3;
constexpr std::uint8_t kStateFailed = 4;

struct VerifiedC6Image {
    std::size_t image_size;
    esp_app_desc_t app_description;
    std::array<std::uint8_t, 32> sha256;
};

std::atomic<bool> update_in_progress{false};
std::array<std::uint8_t, EH_HOST_CP_OTA_CHUNK_MAX> ota_chunk{};

std::uint32_t pack_version(std::uint32_t major,
                           std::uint32_t minor,
                           std::uint32_t patch) noexcept {
    return ((major & 0xffU) << 24U) | ((minor & 0xffU) << 16U) |
           ((patch & 0xffU) << 8U);
}

bool is_target_version(std::uint32_t major,
                       std::uint32_t minor,
                       std::uint32_t patch) noexcept {
    return major == kTargetMajor && minor == kTargetMinor &&
           patch >= kTargetPatch;
}

esp_err_t hash_partition_image(const esp_partition_t* partition,
                               std::size_t image_size,
                               std::array<std::uint8_t, 32>* hash) noexcept {
    if (partition == nullptr || hash == nullptr || image_size > partition->size) {
        return ESP_ERR_INVALID_ARG;
    }
    const mbedtls_md_info_t* digest_info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (digest_info == nullptr) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    mbedtls_md_context_t context{};
    mbedtls_md_init(&context);
    int digest_status = mbedtls_md_setup(&context, digest_info, 0);
    if (digest_status == 0) {
        digest_status = mbedtls_md_starts(&context);
    }
    std::array<std::uint8_t, 512> buffer{};
    std::size_t offset = 0;
    while (digest_status == 0 && offset < image_size) {
        const std::size_t chunk_size =
            std::min(buffer.size(), image_size - offset);
        const esp_err_t read_status = esp_partition_read(
            partition, offset, buffer.data(), chunk_size);
        if (read_status != ESP_OK) {
            mbedtls_md_free(&context);
            return read_status;
        }
        digest_status = mbedtls_md_update(&context, buffer.data(), chunk_size);
        offset += chunk_size;
    }
    if (digest_status == 0) {
        digest_status = mbedtls_md_finish(&context, hash->data());
    }
    mbedtls_md_free(&context);
    return digest_status == 0 ? ESP_OK : ESP_FAIL;
}

const char* audit_state_name(C6OtaAuditState state) noexcept {
    switch (state) {
        case C6OtaAuditState::started: return "started";
        case C6OtaAuditState::pending_verify: return "pending_verify";
        case C6OtaAuditState::verified: return "verified";
        case C6OtaAuditState::failed: return "failed";
        default: return "not_run";
    }
}

esp_err_t inspect_c6_image(const esp_partition_t* partition,
                           VerifiedC6Image* verified_image) noexcept {
    if (partition == nullptr || verified_image == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_image_header_t image_header{};
    esp_err_t status = esp_partition_read(partition, 0, &image_header,
                                          sizeof(image_header));
    if (status != ESP_OK) {
        return status;
    }
    if (image_header.magic != ESP_IMAGE_HEADER_MAGIC ||
        image_header.segment_count == 0 ||
        image_header.segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t offset = sizeof(image_header);
    std::size_t image_size = sizeof(image_header);
    esp_app_desc_t app_description{};

    for (std::size_t segment_index = 0;
         segment_index < image_header.segment_count; ++segment_index) {
        if (offset > partition->size ||
            sizeof(esp_image_segment_header_t) > partition->size - offset) {
            return ESP_ERR_INVALID_SIZE;
        }

        esp_image_segment_header_t segment_header{};
        status = esp_partition_read(partition, offset, &segment_header,
                                    sizeof(segment_header));
        if (status != ESP_OK) {
            return status;
        }

        const std::size_t segment_size = segment_header.data_len;
        const std::size_t segment_header_size = sizeof(segment_header);
        if (segment_size > partition->size - offset - segment_header_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (segment_index == 0) {
            if (segment_size < sizeof(app_description)) {
                return ESP_ERR_INVALID_SIZE;
            }
            status = esp_partition_read(partition,
                                        offset + segment_header_size,
                                        &app_description,
                                        sizeof(app_description));
            if (status != ESP_OK) {
                return status;
            }
        }

        offset += segment_header_size + segment_size;
        image_size = offset;
    }

    ++image_size;
    if (image_header.hash_appended == 1) {
        image_size += (16U - (image_size % 16U)) % 16U;
        image_size += ESP_IMAGE_HASH_LEN;
    }

    if (image_size > partition->size ||
        app_description.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        std::strcmp(app_description.project_name, kExpectedProject) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    std::array<std::uint8_t, 32> image_sha256{};
    status = hash_partition_image(partition, image_size, &image_sha256);
    if (status != ESP_OK) {
        return status;
    }

    verified_image->image_size = image_size;
    verified_image->app_description = app_description;
    verified_image->sha256 = image_sha256;
    return ESP_OK;
}

esp_err_t store_audit(std::uint8_t state,
                      esp_err_t result,
                      std::uint32_t source_version,
                      const VerifiedC6Image* image,
                      bool increment_attempts) noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }

    if (increment_attempts) {
        std::uint32_t attempts = 0;
        status = nvs_get_u32(handle, "attempts", &attempts);
        if (status == ESP_ERR_NVS_NOT_FOUND) {
            attempts = 0;
            status = ESP_OK;
        }
        if (status == ESP_OK) {
            status = nvs_set_u32(handle, "attempts", attempts + 1U);
        }
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, "state", state);
    }
    if (status == ESP_OK) {
        status = nvs_set_i32(handle, "last_error", static_cast<std::int32_t>(result));
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, "source_ver", source_version);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, "target_ver",
                             pack_version(kTargetMajor, kTargetMinor, kTargetPatch));
    }
    if (status == ESP_OK && image != nullptr) {
        status = nvs_set_u32(handle, "image_size",
                             static_cast<std::uint32_t>(image->image_size));
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, "app_elf_hash",
                                  image->app_description.app_elf_sha256,
                                  sizeof(image->app_description.app_elf_sha256));
        }
        if (status == ESP_OK) {
            status = nvs_set_str(handle, "app_version",
                                 image->app_description.version);
        }
            if (status == ESP_OK) {
                status = nvs_set_blob(handle, "image_sha256",
                                      image->sha256.data(), image->sha256.size());
            }
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }

    nvs_close(handle);
    return status;
}

int c6_ota_command(int argc, char** argv) {
    const bool multipan_confirmation = argc == 2 &&
        std::strcmp(argv[1], kMultipanConfirmation) == 0;
    const bool sdio_zigbee_confirmation = argc == 2 &&
        std::strcmp(argv[1], kSdioZigbeeConfirmation) == 0;
    const bool sdio_zigbee_bdb_timeout_confirmation = argc == 2 &&
        std::strcmp(argv[1], kSdioZigbeeBdbTimeoutConfirmation) == 0;
    const bool sdio_zigbee_bdb_trace_confirmation = argc == 2 &&
        std::strcmp(argv[1], kSdioZigbeeBdbTraceConfirmation) == 0;
    const bool sdio_zigbee_bdb_v3_confirmation = argc == 2 &&
        std::strcmp(argv[1], kSdioZigbeeBdbV3Confirmation) == 0;
    const bool h2_wifi_only_confirmation = argc == 2 &&
        std::strcmp(argv[1], kH2WifiOnlyConfirmation) == 0;
    const bool standard_confirmation = argc == 2 &&
        std::strcmp(argv[1], kConfirmation) == 0;
    if (!multipan_confirmation && !sdio_zigbee_confirmation &&
        !sdio_zigbee_bdb_timeout_confirmation &&
        !sdio_zigbee_bdb_trace_confirmation &&
        !sdio_zigbee_bdb_v3_confirmation && !h2_wifi_only_confirmation &&
        !standard_confirmation) {
        std::printf("Usage: c6_ota %s | c6_ota %s | c6_ota %s | c6_ota %s | c6_ota %s | c6_ota %s | c6_ota %s\n",
                    kConfirmation, kMultipanConfirmation,
                    kSdioZigbeeConfirmation,
                    kSdioZigbeeBdbTimeoutConfirmation,
                    kSdioZigbeeBdbTraceConfirmation,
                    kSdioZigbeeBdbV3Confirmation,
                    kH2WifiOnlyConfirmation);
        return 1;
    }
    bool expected = false;
    if (!update_in_progress.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
        std::printf("C6 OTA is already running.\n");
        return 1;
    }

    esp_hosted_coprocessor_fwver_t current_version{};
    const esp_err_t version_status = esp_hosted_get_coprocessor_fwversion(&current_version);
    if (version_status != ESP_OK) {
        ESP_LOGE(kTag, "Cannot read current C6 firmware version: %s",
                 esp_err_to_name(version_status));
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    const bool current_version_is_newer =
        current_version.major1 > kTargetMajor ||
        (current_version.major1 == kTargetMajor &&
         current_version.minor1 > kTargetMinor) ||
        (current_version.major1 == kTargetMajor &&
         current_version.minor1 == kTargetMinor &&
         current_version.patch1 > kTargetPatch);
    const bool current_version_is_same =
        current_version.major1 == kTargetMajor &&
        current_version.minor1 == kTargetMinor &&
        current_version.patch1 == kTargetPatch;
    const bool hash_bound_confirmation =
        multipan_confirmation || sdio_zigbee_confirmation ||
        sdio_zigbee_bdb_timeout_confirmation ||
        sdio_zigbee_bdb_trace_confirmation ||
        sdio_zigbee_bdb_v3_confirmation || h2_wifi_only_confirmation;
    if (current_version_is_newer ||
        (current_version_is_same && !hash_bound_confirmation)) {
        ESP_LOGW(kTag, "Refusing downgrade or duplicate update from %lu.%lu.%lu",
                 static_cast<unsigned long>(current_version.major1),
                 static_cast<unsigned long>(current_version.minor1),
                 static_cast<unsigned long>(current_version.patch1));
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    const esp_partition_t* image_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40),
        kPartitionLabel);
    if (image_partition == nullptr) {
        ESP_LOGE(kTag, "Image partition '%s' was not found", kPartitionLabel);
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    VerifiedC6Image image{};
    esp_err_t status = inspect_c6_image(image_partition, &image);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 image structure or project validation failed: %s",
                 esp_err_to_name(status));
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    const bool staged_image_is_multipan =
        image.sha256 == kMultipanImageSha256;
    const bool staged_image_is_sdio_zigbee =
        image.sha256 == kSdioZigbeeImageSha256;
    const bool staged_image_is_sdio_zigbee_bdb_timeout =
        image.sha256 == kSdioZigbeeBdbTimeoutImageSha256;
    const bool staged_image_is_sdio_zigbee_bdb_trace =
        image.sha256 == kSdioZigbeeBdbTraceImageSha256;
    const bool staged_image_is_sdio_zigbee_bdb_v3 =
        image.sha256 == kSdioZigbeeBdbV3ImageSha256;
    const bool staged_image_is_h2_wifi_only =
        image.sha256 == kH2WifiOnlyImageSha256;
    const bool staged_image_matches_confirmation =
        (multipan_confirmation && staged_image_is_multipan) ||
        (sdio_zigbee_confirmation && staged_image_is_sdio_zigbee) ||
        (sdio_zigbee_bdb_timeout_confirmation &&
         staged_image_is_sdio_zigbee_bdb_timeout) ||
        (sdio_zigbee_bdb_trace_confirmation &&
         staged_image_is_sdio_zigbee_bdb_trace) ||
        (sdio_zigbee_bdb_v3_confirmation &&
         staged_image_is_sdio_zigbee_bdb_v3) ||
        (h2_wifi_only_confirmation && staged_image_is_h2_wifi_only) ||
        (standard_confirmation && !staged_image_is_multipan &&
         !staged_image_is_sdio_zigbee &&
         !staged_image_is_sdio_zigbee_bdb_timeout &&
         !staged_image_is_sdio_zigbee_bdb_trace &&
         !staged_image_is_sdio_zigbee_bdb_v3 &&
         !staged_image_is_h2_wifi_only);
    if (!staged_image_matches_confirmation) {
        ESP_LOGE(kTag, "Confirmation token does not match the staged image SHA-256");
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    ESP_LOGW(kTag, "Confirmed C6 update: %lu.%lu.%lu -> ESP-Hosted CP 3.1.0",
             static_cast<unsigned long>(current_version.major1),
             static_cast<unsigned long>(current_version.minor1),
             static_cast<unsigned long>(current_version.patch1));
    ESP_LOGI(kTag, "Verified C6 app '%s', version '%s', size %lu bytes",
             image.app_description.project_name, image.app_description.version,
             static_cast<unsigned long>(image.image_size));
    char image_hash_hex[65]{};
    for (std::size_t index = 0; index < image.sha256.size(); ++index) {
        std::snprintf(image_hash_hex + index * 2, 3, "%02x",
                      static_cast<unsigned>(image.sha256[index]));
    }
    ESP_LOGI(kTag, "Verified C6 image SHA-256: %s", image_hash_hex);

    status = store_audit(kStateStarted, ESP_OK, pack_version(current_version.major1,
                                                              current_version.minor1,
                                                              current_version.patch1),
                         &image, true);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "Could not persist OTA audit start: %s", esp_err_to_name(status));
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    status = esp_hosted_slave_ota_begin();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 OTA begin failed: %s", esp_err_to_name(status));
        (void)store_audit(kStateFailed, status, pack_version(current_version.major1,
                                                             current_version.minor1,
                                                             current_version.patch1),
                          &image, false);
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    std::uint32_t offset = 0;
    while (offset < image.image_size) {
        const std::uint32_t remaining =
            static_cast<std::uint32_t>(image.image_size - offset);
        const std::uint32_t chunk_size = static_cast<std::uint32_t>(
            std::min<std::size_t>(remaining, ota_chunk.size()));
        status = esp_partition_read(image_partition, offset, ota_chunk.data(), chunk_size);
        if (status == ESP_OK) {
            status = esp_hosted_slave_ota_write(ota_chunk.data(), chunk_size);
        }
        if (status != ESP_OK) {
            ESP_LOGE(kTag, "C6 OTA failed at offset %lu: %s",
                     static_cast<unsigned long>(offset), esp_err_to_name(status));
            (void)esp_hosted_slave_ota_end();
            (void)store_audit(kStateFailed, status, pack_version(current_version.major1,
                                                                 current_version.minor1,
                                                                 current_version.patch1),
                              &image, false);
            update_in_progress.store(false, std::memory_order_release);
            return 1;
        }
        offset += chunk_size;
        if ((offset % (64U * 1024U)) < chunk_size || offset == image.image_size) {
            ESP_LOGI(kTag, "C6 OTA progress: %lu/%lu bytes",
                     static_cast<unsigned long>(offset),
                     static_cast<unsigned long>(image.image_size));
        }
    }

    status = esp_hosted_slave_ota_end();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 OTA end failed: %s", esp_err_to_name(status));
        (void)store_audit(kStateFailed, status, pack_version(current_version.major1,
                                                             current_version.minor1,
                                                             current_version.patch1),
                          &image, false);
        update_in_progress.store(false, std::memory_order_release);
        return 1;
    }

    status = store_audit(kStatePendingVerify, ESP_OK,
                         pack_version(current_version.major1, current_version.minor1,
                                      current_version.patch1),
                         &image, false);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 OTA transferred, but pending audit commit failed: %s",
                 esp_err_to_name(status));
    }

    const bool activate_supported = current_version.major1 > 2 ||
        (current_version.major1 == 2 && current_version.minor1 > 5);
    if (activate_supported) {
        status = esp_hosted_slave_ota_activate();
        if (status != ESP_OK) {
            ESP_LOGE(kTag, "C6 OTA activation failed: %s", esp_err_to_name(status));
            (void)store_audit(kStateFailed, status,
                              pack_version(current_version.major1, current_version.minor1,
                                           current_version.patch1),
                              &image, false);
            update_in_progress.store(false, std::memory_order_release);
            return 1;
        }
    } else {
        ESP_LOGW(kTag, "C6 firmware predates OTA activate RPC; restarting host to resync");
    }

    ESP_LOGW(kTag, "C6 image transferred; restarting P4 to verify coprocessor firmware");
    const int deinit_status = esp_hosted_deinit();
    if (deinit_status != ESP_OK) {
        ESP_LOGW(kTag, "ESP-Hosted deinit returned %d before restart", deinit_status);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return 0;
}

}

esp_err_t read_c6_diagnostics(C6DiagnosticsSnapshot* snapshot) noexcept {
    if (snapshot == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *snapshot = {};
    snapshot->ota_in_progress = update_in_progress.load(std::memory_order_acquire);

    esp_hosted_coprocessor_fwver_t version{};
    const int version_status = esp_hosted_get_coprocessor_fwversion(&version);
    snapshot->version_error = static_cast<esp_err_t>(version_status);
    snapshot->version_valid = version_status == ESP_OK;
    if (snapshot->version_valid) {
        snapshot->version_major = version.major1;
        snapshot->version_minor = version.minor1;
        snapshot->version_patch = version.patch1;
    }

    const int info_status = esp_hosted_get_cp_info(
        &snapshot->chip_id, snapshot->target.data(), snapshot->target.size());
    snapshot->target.back() = '\0';
    snapshot->cp_info_error = static_cast<esp_err_t>(info_status);
    snapshot->cp_info_valid = info_status == ESP_OK &&
                              (snapshot->chip_id != 0 || snapshot->target[0] != '\0');

    const esp_partition_t* image_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40),
        kPartitionLabel);
    if (image_partition == nullptr) {
        snapshot->staged_image_error = ESP_ERR_NOT_FOUND;
    } else {
        snapshot->staging_partition_found = true;
        snapshot->staging_partition_bytes = image_partition->size;
        VerifiedC6Image image{};
        snapshot->staged_image_error = inspect_c6_image(image_partition, &image);
        snapshot->staged_image_valid = snapshot->staged_image_error == ESP_OK;
        if (snapshot->staged_image_valid) {
            snapshot->staged_image_bytes = static_cast<std::uint32_t>(image.image_size);
            std::snprintf(snapshot->staged_app_version.data(),
                          snapshot->staged_app_version.size(), "%.*s",
                          static_cast<int>(sizeof(image.app_description.version)),
                          image.app_description.version);
                snapshot->staged_image_sha256 = image.sha256;
                snapshot->staged_image_hash_valid = true;
        }
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        std::uint8_t state = 0;
        if (nvs_get_u8(handle, "state", &state) == ESP_OK) {
            snapshot->audit_available = true;
            switch (state) {
                case kStateStarted: snapshot->audit_state = C6OtaAuditState::started; break;
                case kStatePendingVerify:
                    snapshot->audit_state = C6OtaAuditState::pending_verify;
                    break;
                case kStateVerified: snapshot->audit_state = C6OtaAuditState::verified; break;
                case kStateFailed: snapshot->audit_state = C6OtaAuditState::failed; break;
                default: snapshot->audit_state = C6OtaAuditState::unknown; break;
            }
        }
        (void)nvs_get_u32(handle, "attempts", &snapshot->ota_attempts);
        (void)nvs_get_i32(handle, "last_error", &snapshot->ota_last_error);
        nvs_close(handle);
    }
    return ESP_OK;
}

esp_err_t register_c6_ota_command() noexcept {
    esp_console_cmd_t command{};
    command.command = "c6_ota";
    command.help = "Update integrated C6 over ESP-Hosted SDIO; requires exact confirmation token";
    command.hint = "CONFIRM-C6-ESP-HOSTED-3.1.0";
    command.func = c6_ota_command;
    return esp_console_cmd_register(&command);
}

esp_err_t register_c6_diagnostics_command() noexcept {
    esp_console_cmd_t command{};
    command.command = "c6_status";
    command.help = "Read-only ESP-Hosted C6 and OTA staging diagnostics";
    command.func = [](int argc, char**) -> int {
        if (argc != 1) {
            std::printf("Usage: c6_status\n");
            return 1;
        }
        C6DiagnosticsSnapshot snapshot{};
        const esp_err_t status = read_c6_diagnostics(&snapshot);
        if (status != ESP_OK) {
            std::printf("C6 diagnostics failed: %s\n", esp_err_to_name(status));
            return 1;
        }
        if (snapshot.version_valid) {
            std::printf("C6 ESP-Hosted CP: %lu.%lu.%lu\n",
                        static_cast<unsigned long>(snapshot.version_major),
                        static_cast<unsigned long>(snapshot.version_minor),
                        static_cast<unsigned long>(snapshot.version_patch));
        } else {
            std::printf("C6 ESP-Hosted CP unavailable: %s\n",
                        esp_err_to_name(snapshot.version_error));
        }
        if (snapshot.cp_info_valid) {
            std::printf("C6 target: %s chip_id=0x%08lx\n", snapshot.target.data(),
                        static_cast<unsigned long>(snapshot.chip_id));
        } else {
            std::printf("C6 target info unavailable: %s\n",
                        esp_err_to_name(snapshot.cp_info_error));
        }
        std::printf("C6 staging: found=%s size=%lu image=%s image_size=%lu app_version=%s image_error=%s\n",
                    snapshot.staging_partition_found ? "yes" : "no",
                    static_cast<unsigned long>(snapshot.staging_partition_bytes),
                    snapshot.staged_image_valid ? "valid" : "invalid",
                    static_cast<unsigned long>(snapshot.staged_image_bytes),
                    snapshot.staged_image_valid ? snapshot.staged_app_version.data() : "-",
                    esp_err_to_name(snapshot.staged_image_error));
            if (snapshot.staged_image_hash_valid) {
                std::printf("C6 staged image SHA-256: ");
                for (const std::uint8_t byte : snapshot.staged_image_sha256) {
                    std::printf("%02x", static_cast<unsigned>(byte));
                }
                std::printf("\n");
            }
        std::printf("C6 OTA: in_progress=%s audit=%s attempts=%lu last_error=%ld\n",
                    snapshot.ota_in_progress ? "yes" : "no",
                    audit_state_name(snapshot.audit_state),
                    static_cast<unsigned long>(snapshot.ota_attempts),
                    static_cast<long>(snapshot.ota_last_error));
        return 0;
    };
    return esp_console_cmd_register(&command);
}

void verify_c6_ota_after_boot(std::uint32_t major,
                              std::uint32_t minor,
                              std::uint32_t patch) noexcept {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    std::uint8_t state = 0;
    const esp_err_t read_status = nvs_get_u8(handle, "state", &state);
    nvs_close(handle);
    if (read_status != ESP_OK ||
        (state != kStateStarted && state != kStatePendingVerify)) {
        return;
    }

    if (is_target_version(major, minor, patch)) {
        ESP_LOGI(kTag, "C6 OTA verified after reboot: %lu.%lu.%lu",
                 static_cast<unsigned long>(major), static_cast<unsigned long>(minor),
                 static_cast<unsigned long>(patch));
        (void)store_audit(kStateVerified, ESP_OK, pack_version(major, minor, patch),
                          nullptr, false);
    } else {
        ESP_LOGE(kTag, "C6 OTA verification failed after reboot; running %lu.%lu.%lu",
                 static_cast<unsigned long>(major), static_cast<unsigned long>(minor),
                 static_cast<unsigned long>(patch));
        (void)store_audit(kStateFailed, ESP_ERR_INVALID_STATE,
                          pack_version(major, minor, patch), nullptr, false);
    }
}

}