#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace hub::wifi {

constexpr std::size_t kMaxAccessPoints = 16;

enum class ProvisioningState : std::uint8_t {
    Starting,
    Scanning,
    NetworksReady,
    Connecting,
    Connected,
    Failed,
};

struct AccessPoint {
    char ssid[33]{};
    std::int8_t rssi = 0;
    bool secured = false;
};

struct ProvisioningUpdate {
    ProvisioningState state = ProvisioningState::Starting;
    std::int8_t rssi_dbm = 0;
    bool rssi_valid = false;
    std::uint8_t network_count = 0;
    bool credential_store_ready = false;
    bool credentials_saved = false;
    std::array<AccessPoint, kMaxAccessPoints> networks{};
    char ipv4_address[16]{};
};

class Provisioning final {
public:
    Provisioning() = default;
    Provisioning(const Provisioning&) = delete;
    Provisioning& operator=(const Provisioning&) = delete;

    // Contract: start once after esp_netif_init(), the default event loop, and ESP-Hosted.
    // The wifi_provision task owns ESP-Hosted STA operations. Public calls are nonblocking;
    // commands use a depth-4 queue, scan results are bounded to 16 APs, and the depth-1
    // update mailbox overwrites stale snapshots. connect() copies at most 32 SSID and
    // 63 password bytes; after DHCP, credentials are committed to the dedicated
    // encrypted wifi_cfg NVS partition. Initialization fails closed unless flash
    // encryption is active. Connected snapshots include RSSI dBm sampled by this owner
    // task at most every 5 seconds; SSID/BSSID are not part of the status API. Values are
    // never logged. Queue-full returns ESP_ERR_TIMEOUT; invalid lengths return ESP_ERR_INVALID_SIZE.
    esp_err_t start() noexcept;
    esp_err_t request_scan() noexcept;
    esp_err_t connect(const char* ssid, const char* password) noexcept;
    bool receive_update(ProvisioningUpdate* update) noexcept;
    bool read_snapshot(ProvisioningUpdate* update) noexcept;
    esp_netif_t* station_netif() const noexcept;

private:
    enum class CommandType : std::uint8_t {
        Scan,
        Connect,
    };

    struct Command {
        CommandType type = CommandType::Scan;
        char ssid[33]{};
        char password[65]{};
    };

    enum class BackendEventType : std::uint8_t {
        StationStarted,
        ScanComplete,
        GotIp,
        Disconnected,
    };

    struct BackendEvent {
        BackendEventType type = BackendEventType::StationStarted;
        std::uint32_t ipv4 = 0;
    };

    static void task_entry(void* context) noexcept;
    static void event_handler(void* context, esp_event_base_t event_base,
                              std::int32_t event_id, void* event_data) noexcept;
    void run() noexcept;
    esp_err_t initialize_remote() noexcept;
    esp_err_t initialize_credential_store() noexcept;
    esp_err_t load_saved_credentials(Command* command, bool* found) noexcept;
    esp_err_t persist_credentials(const char* ssid, const char* password) noexcept;
    void handle_command(Command& command) noexcept;
    void handle_backend_event(const BackendEvent& event) noexcept;
    void begin_scan() noexcept;
    void load_scan_results() noexcept;
    void begin_connection(Command& command, bool persist_on_success = true) noexcept;
    void publish(ProvisioningState state) noexcept;

    QueueHandle_t command_queue_ = nullptr;
    QueueHandle_t backend_event_queue_ = nullptr;
    QueueHandle_t update_queue_ = nullptr;
    SemaphoreHandle_t snapshot_mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    ProvisioningUpdate latest_update_{};
    ProvisioningUpdate snapshot_{};
    esp_netif_t* station_netif_ = nullptr;
    Command startup_command_{};
    char pending_ssid_[33]{};
    char pending_password_[65]{};
    std::uint8_t retry_count_ = 0;
    bool has_saved_credentials_ = false;
    bool credential_store_ready_ = false;
    bool persist_on_success_ = false;
    bool pending_credentials_valid_ = false;
    bool connection_pending_ = false;
    bool started_ = false;
};

}