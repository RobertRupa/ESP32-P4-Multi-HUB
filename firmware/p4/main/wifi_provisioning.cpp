#include "wifi_provisioning.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_flash_encrypt.h"
#include "esp_partition.h"
#include "injected/esp_wifi.h"
#include "esp_wifi_remote.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "wifi_provisioning";
constexpr std::uint8_t kMaxRetries = 3;
constexpr std::uint32_t kCommandQueueLength = 4;
constexpr std::uint32_t kBackendQueueLength = 8;
constexpr std::uint32_t kRetryDelayMs = 700;
constexpr std::uint32_t kSignalPollPeriodMs = 5000;
constexpr char kWifiKeysPartition[] = "wifi_keys";
constexpr char kWifiNvsPartition[] = "wifi_cfg";
constexpr char kWifiNvsNamespace[] = "wifi";

}

namespace hub::wifi {

esp_err_t Provisioning::start() noexcept {
    if (started_) {
        return ESP_ERR_INVALID_STATE;
    }

    command_queue_ = xQueueCreate(kCommandQueueLength, sizeof(Command));
    backend_event_queue_ = xQueueCreate(kBackendQueueLength, sizeof(BackendEvent));
    update_queue_ = xQueueCreate(1, sizeof(ProvisioningUpdate));
    snapshot_mutex_ = xSemaphoreCreateMutex();
    if (command_queue_ == nullptr || backend_event_queue_ == nullptr ||
        update_queue_ == nullptr || snapshot_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    publish(ProvisioningState::Starting);
    if (xTaskCreate(task_entry, "wifi_provision", 5120, this, 4, &task_) != pdPASS) {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    started_ = true;
    return ESP_OK;
}

esp_err_t Provisioning::request_scan() noexcept {
    if (!started_) {
        return ESP_ERR_INVALID_STATE;
    }
    const Command command{.type = CommandType::Scan};
    return xQueueSend(command_queue_, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t Provisioning::connect(const char* ssid, const char* password) noexcept {
    if (!started_ || ssid == nullptr || password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t ssid_length = 0;
    while (ssid_length <= 32 && ssid[ssid_length] != '\0') {
        ++ssid_length;
    }
    std::size_t password_length = 0;
    while (password_length <= 63 && password[password_length] != '\0') {
        ++password_length;
    }
    if (ssid_length == 0 || ssid_length > 32 || password_length > 63) {
        return ESP_ERR_INVALID_SIZE;
    }

    Command command{};
    command.type = CommandType::Connect;
    std::memcpy(command.ssid, ssid, ssid_length);
    std::memcpy(command.password, password, password_length);
    const BaseType_t queued = xQueueSend(command_queue_, &command, 0);
    std::memset(command.password, 0, sizeof(command.password));
    return queued == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool Provisioning::receive_update(ProvisioningUpdate* update) noexcept {
    return update != nullptr && update_queue_ != nullptr &&
           xQueueReceive(update_queue_, update, 0) == pdTRUE;
}

bool Provisioning::read_snapshot(ProvisioningUpdate* update) noexcept {
    if (update == nullptr || snapshot_mutex_ == nullptr ||
        xSemaphoreTake(snapshot_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    *update = snapshot_;
    xSemaphoreGive(snapshot_mutex_);
    return true;
}

esp_netif_t* Provisioning::station_netif() const noexcept {
    if (snapshot_mutex_ == nullptr ||
        xSemaphoreTake(snapshot_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return nullptr;
    }
    esp_netif_t* netif = station_netif_;
    xSemaphoreGive(snapshot_mutex_);
    return netif;
}

void Provisioning::task_entry(void* context) noexcept {
    static_cast<Provisioning*>(context)->run();
}

void Provisioning::event_handler(void* context, esp_event_base_t event_base,
                                 std::int32_t event_id, void* event_data) noexcept {
    auto* self = static_cast<Provisioning*>(context);
    if (self == nullptr || self->backend_event_queue_ == nullptr) {
        return;
    }

    BackendEvent event{};
    if (event_base == WIFI_REMOTE_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            event.type = BackendEventType::StationStarted;
        } else if (event_id == WIFI_EVENT_SCAN_DONE) {
            event.type = BackendEventType::ScanComplete;
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            event.type = BackendEventType::Disconnected;
        } else {
            return;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* ip_event = static_cast<const ip_event_got_ip_t*>(event_data);
        if (ip_event == nullptr) {
            return;
        }
        event.type = BackendEventType::GotIp;
        event.ipv4 = ip_event->ip_info.ip.addr;
    } else {
        return;
    }

    (void)xQueueSend(self->backend_event_queue_, &event, 0);
}

void Provisioning::run() noexcept {
    const esp_err_t status = initialize_remote();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "ESP-Hosted Wi-Fi initialization failed: %s", esp_err_to_name(status));
        publish(ProvisioningState::Failed);
        vTaskDelete(nullptr);
    }

    TickType_t last_signal_sample = xTaskGetTickCount();
    for (;;) {
        Command command{};
        if (xQueueReceive(command_queue_, &command, pdMS_TO_TICKS(25)) == pdTRUE) {
            handle_command(command);
            std::memset(command.password, 0, sizeof(command.password));
        }

        BackendEvent event{};
        while (xQueueReceive(backend_event_queue_, &event, 0) == pdTRUE) {
            handle_backend_event(event);
        }

        const TickType_t now = xTaskGetTickCount();
        if (latest_update_.state == ProvisioningState::Connected &&
            now - last_signal_sample >= pdMS_TO_TICKS(kSignalPollPeriodMs)) {
            publish(ProvisioningState::Connected);
            last_signal_sample = now;
        }
    }
}

esp_err_t Provisioning::initialize_remote() noexcept {
    esp_log_level_set("eh_wifi", ESP_LOG_WARN);
    esp_err_t status = initialize_credential_store();
    if (status != ESP_OK) {
        return status;
    }

    status = load_saved_credentials(&startup_command_, &has_saved_credentials_);
    if (status != ESP_OK) {
        return status;
    }
    latest_update_.credential_store_ready = credential_store_ready_;
    latest_update_.credentials_saved = has_saved_credentials_;

    station_netif_ = esp_netif_create_default_wifi_sta();
    if (station_netif_ == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    status = esp_wifi_remote_init(&wifi_config);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_wifi_remote_set_storage(WIFI_STORAGE_RAM);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_event_handler_register(WIFI_REMOTE_EVENT, ESP_EVENT_ANY_ID,
                                        event_handler, this);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        event_handler, this);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_wifi_remote_set_mode(WIFI_MODE_STA);
    if (status != ESP_OK) {
        return status;
    }
    return esp_wifi_remote_start();
}

void Provisioning::handle_command(Command& command) noexcept {
    if (command.type == CommandType::Scan) {
        begin_scan();
    } else {
        begin_connection(command);
    }
}

void Provisioning::handle_backend_event(const BackendEvent& event) noexcept {
    switch (event.type) {
        case BackendEventType::StationStarted:
            if (has_saved_credentials_) {
                has_saved_credentials_ = false;
                begin_connection(startup_command_, false);
                std::memset(startup_command_.password, 0, sizeof(startup_command_.password));
            } else {
                begin_scan();
            }
            break;
        case BackendEventType::ScanComplete:
            load_scan_results();
            break;
        case BackendEventType::GotIp: {
            connection_pending_ = false;
            if (pending_credentials_valid_) {
                if (persist_on_success_) {
                    const esp_err_t persist_status = persist_credentials(pending_ssid_, pending_password_);
                    latest_update_.credentials_saved = persist_status == ESP_OK;
                    if (persist_status != ESP_OK) {
                        ESP_LOGE(kTag, "Could not persist encrypted Wi-Fi credentials: %s",
                                 esp_err_to_name(persist_status));
                    }
                } else {
                    latest_update_.credentials_saved = true;
                }
                std::memset(pending_ssid_, 0, sizeof(pending_ssid_));
                std::memset(pending_password_, 0, sizeof(pending_password_));
                pending_credentials_valid_ = false;
                persist_on_success_ = false;
            }
            latest_update_.ipv4_address[0] = '\0';
            const esp_ip4_addr_t address{.addr = event.ipv4};
            (void)std::snprintf(latest_update_.ipv4_address,
                                sizeof(latest_update_.ipv4_address),
                                IPSTR, IP2STR(&address));
            publish(ProvisioningState::Connected);
            break;
        }
        case BackendEventType::Disconnected:
            if (!connection_pending_) {
                break;
            }
            if (retry_count_ < kMaxRetries) {
                ++retry_count_;
                vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs * retry_count_));
                const esp_err_t status = esp_wifi_remote_connect();
                if (status == ESP_OK) {
                    publish(ProvisioningState::Connecting);
                    break;
                }
            }
            connection_pending_ = false;
            if (pending_credentials_valid_) {
                std::memset(pending_ssid_, 0, sizeof(pending_ssid_));
                std::memset(pending_password_, 0, sizeof(pending_password_));
                pending_credentials_valid_ = false;
            }
            persist_on_success_ = false;
            latest_update_.credentials_saved = has_saved_credentials_;
            publish(ProvisioningState::Failed);
            break;
    }
}

void Provisioning::begin_scan() noexcept {
    wifi_scan_config_t scan_config{};
    const esp_err_t status = esp_wifi_remote_scan_start(&scan_config, false);
    if (status != ESP_OK) {
        ESP_LOGW(kTag, "Wi-Fi scan request failed: %s", esp_err_to_name(status));
        publish(ProvisioningState::Failed);
        return;
    }
    publish(ProvisioningState::Scanning);
}

void Provisioning::load_scan_results() noexcept {
    std::uint16_t available = 0;
    esp_err_t status = esp_wifi_remote_scan_get_ap_num(&available);
    if (status != ESP_OK) {
        publish(ProvisioningState::Failed);
        return;
    }

    const std::uint16_t count = static_cast<std::uint16_t>(
        std::min<std::size_t>(available, kMaxAccessPoints));
    std::array<wifi_ap_record_t, kMaxAccessPoints> records{};
    std::uint16_t fetched = count;
    if (count > 0) {
        status = esp_wifi_remote_scan_get_ap_records(&fetched, records.data());
        if (status != ESP_OK) {
            publish(ProvisioningState::Failed);
            return;
        }
    }

    latest_update_.network_count = static_cast<std::uint8_t>(fetched);
    latest_update_.networks.fill(AccessPoint{});
    for (std::uint16_t index = 0; index < fetched; ++index) {
        auto& network = latest_update_.networks[index];
        std::memcpy(network.ssid, records[index].ssid, sizeof(records[index].ssid));
        network.ssid[sizeof(records[index].ssid)] = '\0';
        network.rssi = records[index].rssi;
        network.secured = records[index].authmode != WIFI_AUTH_OPEN;
    }
    std::sort(latest_update_.networks.begin(),
              latest_update_.networks.begin() + fetched,
              [](const AccessPoint& left, const AccessPoint& right) {
                  return left.rssi > right.rssi;
              });
    publish(ProvisioningState::NetworksReady);
}

void Provisioning::begin_connection(Command& command, bool persist_on_success) noexcept {
    const std::size_t ssid_length = strnlen(command.ssid, sizeof(command.ssid));
    const std::size_t password_length = strnlen(command.password, sizeof(command.password));
    if (ssid_length == 0 || ssid_length >= sizeof(command.ssid) ||
        password_length >= sizeof(command.password)) {
        latest_update_.credentials_saved = false;
        publish(ProvisioningState::Failed);
        return;
    }

    wifi_config_t wifi_config{};
    std::memcpy(wifi_config.sta.ssid, command.ssid, ssid_length);
    std::memcpy(wifi_config.sta.password, command.password, password_length);
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    std::memset(pending_ssid_, 0, sizeof(pending_ssid_));
    std::memset(pending_password_, 0, sizeof(pending_password_));
    std::memcpy(pending_ssid_, command.ssid, ssid_length);
    std::memcpy(pending_password_, command.password, password_length);
    pending_credentials_valid_ = true;
    persist_on_success_ = persist_on_success;
    if (persist_on_success) {
        latest_update_.credentials_saved = false;
    }

    esp_err_t status = esp_wifi_remote_set_config(WIFI_IF_STA, &wifi_config);
    std::memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    if (status == ESP_OK) {
        connection_pending_ = true;
        retry_count_ = 0;
        status = esp_wifi_remote_connect();
    }
    std::memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    std::memset(command.password, 0, sizeof(command.password));

    if (status != ESP_OK) {
        connection_pending_ = false;
        std::memset(pending_ssid_, 0, sizeof(pending_ssid_));
        std::memset(pending_password_, 0, sizeof(pending_password_));
        pending_credentials_valid_ = false;
        persist_on_success_ = false;
        ESP_LOGW(kTag, "Wi-Fi connection request failed: %s", esp_err_to_name(status));
        publish(ProvisioningState::Failed);
        return;
    }
    latest_update_.ipv4_address[0] = '\0';
    publish(ProvisioningState::Connecting);
}

esp_err_t Provisioning::initialize_credential_store() noexcept {
    if (!esp_flash_encryption_enabled()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t* key_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
        kWifiKeysPartition);
    if (key_partition == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    nvs_sec_cfg_t security_config{};
    esp_err_t status = nvs_flash_read_security_cfg(key_partition, &security_config);
    if (status == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        status = nvs_flash_generate_keys(key_partition, &security_config);
    }
    if (status == ESP_OK) {
        status = nvs_flash_secure_init_partition(kWifiNvsPartition, &security_config);
    }
    std::memset(&security_config, 0, sizeof(security_config));
    credential_store_ready_ = status == ESP_OK;
    return status;
}

esp_err_t Provisioning::load_saved_credentials(Command* command, bool* found) noexcept {
    if (command == nullptr || found == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;

    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(kWifiNvsPartition, kWifiNvsNamespace,
                                                NVS_READONLY, &handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (status != ESP_OK) {
        return status;
    }

    std::size_t ssid_size = sizeof(command->ssid);
    std::size_t password_size = sizeof(command->password);
    status = nvs_get_str(handle, "ssid", command->ssid, &ssid_size);
    if (status == ESP_OK) {
        status = nvs_get_str(handle, "password", command->password, &password_size);
    }
    nvs_close(handle);

    if (status == ESP_ERR_NVS_NOT_FOUND) {
        std::memset(command->ssid, 0, sizeof(command->ssid));
        std::memset(command->password, 0, sizeof(command->password));
        return ESP_OK;
    }
    if (status != ESP_OK || ssid_size > sizeof(command->ssid) ||
        password_size > sizeof(command->password) || command->ssid[0] == '\0') {
        std::memset(command->ssid, 0, sizeof(command->ssid));
        std::memset(command->password, 0, sizeof(command->password));
        return status == ESP_OK ? ESP_ERR_INVALID_SIZE : status;
    }

    command->type = CommandType::Connect;
    *found = true;
    return ESP_OK;
}

esp_err_t Provisioning::persist_credentials(const char* ssid, const char* password) noexcept {
    if (!credential_store_ready_ || ssid == nullptr || password == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(kWifiNvsPartition, kWifiNvsNamespace,
                                                NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_str(handle, "ssid", ssid);
    if (status == ESP_OK) {
        status = nvs_set_str(handle, "password", password);
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

void Provisioning::publish(ProvisioningState state) noexcept {
    if (state == ProvisioningState::Connected) {
        wifi_ap_record_t ap_info{};
        latest_update_.rssi_valid =
            esp_wifi_remote_sta_get_ap_info(&ap_info) == ESP_OK;
        if (latest_update_.rssi_valid) {
            latest_update_.rssi_dbm = ap_info.rssi;
        }
    } else {
        latest_update_.rssi_valid = false;
    }
    latest_update_.state = state;
    latest_update_.credential_store_ready = credential_store_ready_;
    if (snapshot_mutex_ != nullptr && xSemaphoreTake(snapshot_mutex_, portMAX_DELAY) == pdTRUE) {
        snapshot_ = latest_update_;
        xSemaphoreGive(snapshot_mutex_);
    }
    if (update_queue_ != nullptr) {
        (void)xQueueOverwrite(update_queue_, &latest_update_);
    }
}

}