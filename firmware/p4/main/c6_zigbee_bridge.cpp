#include "c6_zigbee_bridge.hpp"

#include <cstdio>
#include <cstring>

#include "eh_host_feat_peer_data.h"
#include "esp_console.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

namespace {

constexpr char kTag[] = "c6_zigbee_bridge";
constexpr char kAuditNamespace[] = "c6_zb_audit";
constexpr char kSearchConfirmation[] = "CONFIRM-C6-ZIGBEE-SEARCH";
constexpr char kNetworkConfirmation[] = "CONFIRM-C6-ZIGBEE-NETWORK";
constexpr char kPairingConfirmation[] = "CONFIRM-C6-ZIGBEE-PAIRING";
constexpr std::uint32_t kStatusMessageId = 0x5A420101U;
constexpr std::uint32_t kCommandMessageId = 0x5A420102U;
constexpr std::uint8_t kLegacyStatusProtocolVersion = 1;
constexpr std::uint8_t kStatusProtocolVersion2 = 2;
constexpr std::uint8_t kStatusProtocolVersion = 3;
constexpr std::uint8_t kCommandProtocolVersion = 1;
constexpr std::uint8_t kGetStatusCommand = 0;
constexpr std::uint8_t kSearchCommand = 1;
constexpr std::uint8_t kCreateNetworkCommand = 2;
constexpr std::uint8_t kOpenJoinWindowCommand = 3;
constexpr std::uint8_t kJoinWindowSeconds = 180;

struct PendingCommand {
    std::uint8_t command;
    std::uint8_t duration_seconds;
};

hub::radio::C6ZigbeeBridge* active_bridge = nullptr;

const char* bdb_stage_name(std::uint8_t stage) noexcept {
    switch (stage) {
        case 1: return "initialization";
        case 2: return "formation_start";
        case 3: return "formation_signal";
        case 4: return "close_network";
        case 5: return "open_join_window";
        case 6: return "watchdog_timeout";
        default: return "none";
    }
}

int c6_zigbee_status_command(int argc, char** argv) {
    (void)argv;
    if (argc != 1 || active_bridge == nullptr) {
        std::printf("Usage: c6_zigbee_status\n");
        return 1;
    }
    hub::radio::C6ZigbeeSnapshot snapshot{};
    if (!active_bridge->read_snapshot(&snapshot)) {
        std::printf("C6 Zigbee status unavailable\n");
        return 1;
    }
    std::printf("C6 Zigbee: available=%s stack_ready=%s network_formed=%s "
                "search_pending=%s devices=%u join_seconds=%u last_error=%ld\n",
                snapshot.available ? "yes" : "no",
                snapshot.stack_ready ? "yes" : "no",
                snapshot.network_formed ? "yes" : "no",
                snapshot.search_pending ? "yes" : "no",
                static_cast<unsigned>(snapshot.device_count),
                static_cast<unsigned>(snapshot.pairing_seconds_remaining),
                static_cast<long>(snapshot.last_error));
    std::printf("C6 Zigbee BDB: signal=0x%04x status=%u raw_result=%ld stage=%s(%u)\n",
                static_cast<unsigned>(snapshot.last_signal_type),
                static_cast<unsigned>(snapshot.last_bdb_status),
                static_cast<long>(snapshot.last_bdb_result),
                bdb_stage_name(snapshot.last_bdb_stage),
                static_cast<unsigned>(snapshot.last_bdb_stage));
    return 0;
}

int c6_zigbee_search_command(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], kSearchConfirmation) != 0 ||
        active_bridge == nullptr) {
        std::printf("Usage: c6_zigbee_search %s\n", kSearchConfirmation);
        return 1;
    }
    const esp_err_t status = active_bridge->request_search();
    if (status != ESP_OK) {
        std::printf("C6 Zigbee search request failed: %s\n", esp_err_to_name(status));
        return 1;
    }
    std::printf("C6 Zigbee search request accepted; join window is limited to %u seconds\n",
                static_cast<unsigned>(kJoinWindowSeconds));
    return 0;
}

int c6_zigbee_network_command(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], kNetworkConfirmation) != 0 ||
        active_bridge == nullptr) {
        std::printf("Usage: c6_zigbee_network_start %s\n", kNetworkConfirmation);
        return 1;
    }
    const esp_err_t status = active_bridge->request_network_start();
    if (status != ESP_OK) {
        std::printf("C6 Zigbee network request failed: %s\n", esp_err_to_name(status));
        return 1;
    }
    std::printf("C6 Zigbee network creation request accepted; joining remains closed\n");
    return 0;
}

int c6_zigbee_pairing_command(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], kPairingConfirmation) != 0 ||
        active_bridge == nullptr) {
        std::printf("Usage: c6_zigbee_pairing_window %s\n", kPairingConfirmation);
        return 1;
    }
    const esp_err_t status = active_bridge->request_pairing_window(kJoinWindowSeconds);
    if (status != ESP_OK) {
        std::printf("C6 Zigbee join window request failed: %s\n", esp_err_to_name(status));
        return 1;
    }
    std::printf("C6 Zigbee join window request accepted for %u seconds\n",
                static_cast<unsigned>(kJoinWindowSeconds));
    return 0;
}

}

namespace hub::radio {

esp_err_t C6ZigbeeBridge::start() noexcept {
    if (started_) {
        return ESP_ERR_INVALID_STATE;
    }
    snapshot_mutex_ = xSemaphoreCreateMutex();
    if (snapshot_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t status = eh_host_feat_peer_data_init();
    if (status != ESP_OK) {
        return status;
    }
    status = eh_host_peer_data_register(kStatusMessageId, peer_data_callback, this);
    if (status != ESP_OK) {
        return status;
    }
    command_queue_ = xQueueCreate(2, sizeof(PendingCommand));
    if (command_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(command_task, "c6_zb_host_cmd", 4096, this, 4,
                    &command_task_) != pdPASS) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    started_ = true;
    active_bridge = this;
    return ESP_OK;
}

esp_err_t C6ZigbeeBridge::request_search() noexcept {
    return submit_command(kSearchCommand, kJoinWindowSeconds);
}

esp_err_t C6ZigbeeBridge::request_network_start() noexcept {
    return submit_command(kCreateNetworkCommand, 0);
}

esp_err_t C6ZigbeeBridge::request_pairing_window(
    std::uint8_t duration_seconds) noexcept {
    if (duration_seconds == 0 || duration_seconds > kJoinWindowSeconds) {
        return ESP_ERR_INVALID_ARG;
    }
    return submit_command(kOpenJoinWindowCommand, duration_seconds);
}

esp_err_t C6ZigbeeBridge::submit_command(std::uint8_t command,
                                         std::uint8_t duration_seconds) noexcept {
    if (!started_ || command_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const PendingCommand pending{command, duration_seconds};
    return xQueueSend(command_queue_, &pending, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool C6ZigbeeBridge::read_snapshot(C6ZigbeeSnapshot* snapshot) const noexcept {
    if (snapshot == nullptr || snapshot_mutex_ == nullptr ||
        xSemaphoreTake(snapshot_mutex_, 0) != pdTRUE) {
        return false;
    }
    *snapshot = snapshot_;
    xSemaphoreGive(snapshot_mutex_);
    return true;
}

void C6ZigbeeBridge::peer_data_callback(std::uint32_t message_id,
                                        const std::uint8_t* data,
                                        std::size_t length,
                                        void* context) {
    auto* bridge = static_cast<C6ZigbeeBridge*>(context);
    const bool legacy_payload = data != nullptr && length == 9 &&
        data[0] == kLegacyStatusProtocolVersion;
    const bool extended_v2_payload = data != nullptr && length == 12 &&
        data[0] == kStatusProtocolVersion2;
    const bool extended_v3_payload = data != nullptr && length == 17 &&
        data[0] == kStatusProtocolVersion;
    if (bridge == nullptr || message_id != kStatusMessageId || data == nullptr ||
        (!legacy_payload && !extended_v2_payload && !extended_v3_payload) ||
        xSemaphoreTake(bridge->snapshot_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    bridge->snapshot_.available = true;
    bridge->snapshot_.stack_ready = (data[1] & 0x01U) != 0;
    bridge->snapshot_.network_formed = (data[1] & 0x02U) != 0;
    bridge->snapshot_.search_pending = (data[1] & 0x04U) != 0;
    bridge->snapshot_.formation_timed_out =
        (extended_v2_payload || extended_v3_payload) &&
        (data[1] & 0x08U) != 0;
    bridge->snapshot_.device_count =
        static_cast<std::uint16_t>(data[2]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[3]) << 8U);
    bridge->snapshot_.pairing_seconds_remaining = data[4];
    bridge->snapshot_.last_error = static_cast<esp_err_t>(
        static_cast<std::uint32_t>(data[5]) |
        (static_cast<std::uint32_t>(data[6]) << 8U) |
        (static_cast<std::uint32_t>(data[7]) << 16U) |
        (static_cast<std::uint32_t>(data[8]) << 24U));
    if (extended_v2_payload || extended_v3_payload) {
        bridge->snapshot_.last_signal_type =
            static_cast<std::uint16_t>(data[9]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[10]) << 8U);
        bridge->snapshot_.last_bdb_status = data[11];
    } else {
        bridge->snapshot_.last_signal_type = 0;
        bridge->snapshot_.last_bdb_status = 0;
    }
    if (extended_v3_payload) {
        bridge->snapshot_.last_bdb_result = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(data[12]) |
            (static_cast<std::uint32_t>(data[13]) << 8U) |
            (static_cast<std::uint32_t>(data[14]) << 16U) |
            (static_cast<std::uint32_t>(data[15]) << 24U));
        bridge->snapshot_.last_bdb_stage = data[16];
    } else {
        bridge->snapshot_.last_bdb_result = 0;
        bridge->snapshot_.last_bdb_stage = 0;
    }
    xSemaphoreGive(bridge->snapshot_mutex_);
}

esp_err_t C6ZigbeeBridge::send_command(std::uint8_t command,
                                       std::uint8_t duration_seconds) noexcept {
    const std::uint8_t payload[] = {
        kCommandProtocolVersion, command, duration_seconds,
    };
    return eh_host_peer_data_send(kCommandMessageId, payload, sizeof(payload));
}

void C6ZigbeeBridge::command_task(void* context) {
    auto* bridge = static_cast<C6ZigbeeBridge*>(context);
    if (bridge == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    PendingCommand pending{};
    for (;;) {
        if (xQueueReceive(bridge->command_queue_, &pending, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const bool network_start = pending.command == kCreateNetworkCommand;
        const esp_err_t audit_status = network_start
            ? bridge->persist_audit("network_count", "network_time_s")
            : bridge->persist_audit("search_count", "search_uptime_s");
        if (audit_status != ESP_OK) {
            ESP_LOGE(kTag, "C6 Zigbee command audit failed: %s",
                     esp_err_to_name(audit_status));
            continue;
        }
        const esp_err_t send_status = bridge->send_command(
            pending.command, pending.duration_seconds);
        if (send_status != ESP_OK) {
            ESP_LOGE(kTag, "C6 Zigbee command send failed: %s",
                     esp_err_to_name(send_status));
        }
    }
}

esp_err_t C6ZigbeeBridge::persist_audit(const char* count_key,
                                        const char* time_key) noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open(kAuditNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    std::uint32_t count = 0;
    status = nvs_get_u32(handle, count_key, &count);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        status = ESP_OK;
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, count_key, count + 1U);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, time_key,
                             static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

esp_err_t register_c6_zigbee_console(C6ZigbeeBridge& bridge) noexcept {
    active_bridge = &bridge;
    esp_console_cmd_t status_command{};
    status_command.command = "c6_zigbee_status";
    status_command.help = "Read-only C6 Zigbee coordinator status";
    status_command.func = c6_zigbee_status_command;
    esp_err_t status = esp_console_cmd_register(&status_command);
    if (status != ESP_OK) {
        return status;
    }
    esp_console_cmd_t search_command{};
    search_command.command = "c6_zigbee_search";
    search_command.help = "Form/open C6 Zigbee network for 180 seconds; requires confirmation token";
    search_command.hint = "CONFIRM-C6-ZIGBEE-SEARCH";
    search_command.func = c6_zigbee_search_command;
    status = esp_console_cmd_register(&search_command);
    if (status != ESP_OK) {
        return status;
    }
    esp_console_cmd_t network_command{};
    network_command.command = "c6_zigbee_network_start";
    network_command.help = "Create the C6 Zigbee network; joining remains closed";
    network_command.hint = "CONFIRM-C6-ZIGBEE-NETWORK";
    network_command.func = c6_zigbee_network_command;
    status = esp_console_cmd_register(&network_command);
    if (status != ESP_OK) {
        return status;
    }
    esp_console_cmd_t pairing_command{};
    pairing_command.command = "c6_zigbee_pairing_window";
    pairing_command.help = "Open a 180-second C6 Zigbee join window";
    pairing_command.hint = "CONFIRM-C6-ZIGBEE-PAIRING";
    pairing_command.func = c6_zigbee_pairing_command;
    return esp_console_cmd_register(&pairing_command);
}

}