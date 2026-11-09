#include "multipan_service.hpp"

#include <algorithm>
#include <cstdio>

#include "esp_log.h"
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
#include "driver/uart.h"
#include "esp_vfs_eventfd.h"
#include "ezbee/zha.h"
#include "ezbee/zcl.h"
#include "ezbee/zcl/cluster/level.h"
#include "ezbee/zcl/cluster/on_off.h"
#endif
#if CONFIG_HUB_MULTIPAN_ENABLE
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_radio_spinel_multipan.h"
#include "mdns.h"
#include "openthread/dataset.h"
#include "openthread/thread.h"
#endif
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
#include "esp_radio_spinel.h"
#endif
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "zigbee_search_policy.hpp"
#include "sdkconfig.h"
#if CONFIG_HUB_MULTIPAN_ENABLE
#include "ezbee/zha.h"
#endif

namespace {

constexpr char kTag[] = "multipan";
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
constexpr bool kH2ZigbeeRcpEnabled = true;
#else
constexpr bool kH2ZigbeeRcpEnabled = false;
#endif
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
constexpr char kStoragePartition[] = "nvs";
#else
constexpr char kStoragePartition[] = "wifi_cfg";
#endif
constexpr char kAuditNamespace[] = "radio_audit";
constexpr char kKnownDevicesNamespace[] = "zigbee_known";
constexpr char kKnownDevicesKey[] = "shorts_v1";
constexpr char kNetworkStartUptimeKey[] = "network_start_s";
static_assert(sizeof(kNetworkStartUptimeKey) <= NVS_KEY_NAME_MAX_SIZE);
static_assert(sizeof(kKnownDevicesNamespace) <= NVS_NS_NAME_MAX_SIZE);
static_assert(sizeof(kKnownDevicesKey) <= NVS_KEY_NAME_MAX_SIZE);
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
constexpr char kManufacturer[] = "\x09" "ESPRESSIF";
constexpr char kModel[] = "\x0B" "P4-ZIGBEE-HOST";
#endif
constexpr std::uint8_t kMaxJoinWindowSeconds = 180;
constexpr std::uint32_t kEventQueueDepth = 32;
constexpr std::uint32_t kCommandQueueDepth = 2;
constexpr std::uint32_t kProvisioningWaitMs = 30000;
constexpr std::uint32_t kFormationTimeoutMs = 120000;
constexpr std::uint32_t kZdoInterviewTimeoutMs = 10000;
constexpr std::uint8_t kBdbStageInitialization = 1;
constexpr std::uint8_t kBdbStageFormationStart = 2;
constexpr std::uint8_t kBdbStageFormationSignal = 3;
constexpr std::uint8_t kBdbStageCloseNetwork = 4;
constexpr std::uint8_t kBdbStageTimeout = 6;

#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
uart_config_t make_multipan_uart_config() noexcept {
    uart_config_t config{};
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    config.baud_rate = 230400;
#else
    config.baud_rate = 460800;
#endif
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;
    return config;
}
#endif

}

namespace hub::radio {

MultipanService* MultipanService::active_instance_ = nullptr;

namespace {

#if CONFIG_HUB_MULTIPAN_ENABLE
ThreadRole map_thread_role(otDeviceRole role, bool commissioned) noexcept {
    if (!commissioned) {
        return ThreadRole::uncommissioned;
    }
    switch (role) {
        case OT_DEVICE_ROLE_DISABLED: return ThreadRole::disabled;
        case OT_DEVICE_ROLE_DETACHED: return ThreadRole::detached;
        case OT_DEVICE_ROLE_CHILD: return ThreadRole::child;
        case OT_DEVICE_ROLE_ROUTER: return ThreadRole::router;
        case OT_DEVICE_ROLE_LEADER: return ThreadRole::leader;
        default: return ThreadRole::unknown;
    }
}
#endif

}

MultipanService::MultipanService(wifi::Provisioning& provisioning) noexcept
    : provisioning_(provisioning) {}

esp_err_t MultipanService::start() noexcept {
    if (started_) {
        return ESP_ERR_INVALID_STATE;
    }

    snapshot_mutex_ = xSemaphoreCreateMutex();
    if (snapshot_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    current_.state = MultipanState::disabled;
    publish();
    started_ = true;
    return ESP_OK;
#else
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    if (CONFIG_HUB_H2_ZIGBEE_UART_TX_GPIO < 0 ||
        CONFIG_HUB_H2_ZIGBEE_UART_RX_GPIO < 0 ||
        CONFIG_HUB_H2_ZIGBEE_UART_TX_GPIO == CONFIG_HUB_H2_ZIGBEE_UART_RX_GPIO) {
#else
    if (CONFIG_HUB_MULTIPAN_UART_TX_GPIO < 0 ||
        CONFIG_HUB_MULTIPAN_UART_RX_GPIO < 0 ||
        CONFIG_HUB_MULTIPAN_UART_TX_GPIO == CONFIG_HUB_MULTIPAN_UART_RX_GPIO) {
#endif
        current_.state = MultipanState::needs_uart_configuration;
        current_.last_error = ESP_ERR_INVALID_ARG;
        publish();
        started_ = true;
        return ESP_OK;
    }

    stack_event_queue_ = xQueueCreate(kEventQueueDepth, sizeof(StackEvent));
    command_queue_ = xQueueCreate(kCommandQueueDepth, sizeof(Command));
    if (stack_event_queue_ == nullptr || command_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    if (active_instance_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    active_instance_ = this;
    current_.state = MultipanState::starting;
    publish();

    if (xTaskCreate(owner_task, "multipan_owner", 8192, this, 5,
                    &owner_task_) != pdPASS) {
        active_instance_ = nullptr;
        owner_task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    started_ = true;
    return ESP_OK;
#endif
}

esp_err_t MultipanService::request_pairing_window(
    std::uint8_t duration_seconds) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)duration_seconds;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr || duration_seconds == 0 ||
        duration_seconds > kMaxJoinWindowSeconds) {
        return ESP_ERR_INVALID_ARG;
    }
    const Command command{CommandType::OpenJoinWindow, duration_seconds};
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
#endif
}

esp_err_t MultipanService::request_network_start() noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const Command command{CommandType::StartNetworks, 0};
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
#endif
}

esp_err_t MultipanService::request_search() noexcept {
    if (!started_ || command_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const Command command{CommandType::SearchNearby, 0};
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

esp_err_t MultipanService::request_device_interview(
    std::uint16_t short_address) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr || short_address == 0 ||
        short_address == 0xFFFFU) {
        return ESP_ERR_INVALID_ARG;
    }
    Command command{};
    command.type = CommandType::InterviewDevice;
    command.short_address = short_address;
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
#endif
}

esp_err_t MultipanService::request_device_on_off_read(
    std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    (void)endpoint_id;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr || short_address == 0 ||
        short_address == 0xFFFFU || endpoint_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    Command command{};
    command.type = CommandType::ReadOnOffState;
    command.short_address = short_address;
    command.endpoint_id = endpoint_id;
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
#endif
}

esp_err_t MultipanService::request_device_level_read(
    std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    (void)endpoint_id;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr || short_address == 0 ||
        short_address == 0xFFFFU || endpoint_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    Command command{};
    command.type = CommandType::ReadLevelState;
    command.short_address = short_address;
    command.endpoint_id = endpoint_id;
    return xQueueSend(command_queue_, &command, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
#endif
}

bool MultipanService::read_device_capabilities(
    std::uint16_t short_address,
    ZigbeeDeviceCapabilities* capabilities) const noexcept {
    if (capabilities == nullptr || snapshot_mutex_ == nullptr ||
        xSemaphoreTake(snapshot_mutex_, 0) != pdTRUE) {
        return false;
    }
    const auto end = published_device_capabilities_.begin() +
        published_device_capability_count_;
    const auto device = std::find_if(
        published_device_capabilities_.begin(), end,
        [short_address](const ZigbeeDeviceCapabilities& item) {
            return item.short_address == short_address;
        });
    if (device != end) {
        *capabilities = *device;
    }
    xSemaphoreGive(snapshot_mutex_);
    return device != end;
}

esp_err_t MultipanService::load_known_devices() noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(
        kStoragePartition, kKnownDevicesNamespace, NVS_READONLY, &handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        known_devices_ = {};
        return ESP_OK;
    }
    if (status != ESP_OK) {
        return status;
    }

    std::array<std::uint8_t, kZigbeeKnownDevicesBlobCapacity> blob{};
    std::size_t blob_size = blob.size();
    status = nvs_get_blob(handle, kKnownDevicesKey, blob.data(), &blob_size);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        known_devices_ = {};
        return ESP_OK;
    }
    if (status != ESP_OK) {
        return status;
    }

    ZigbeeKnownDevices loaded{};
    if (!decode_zigbee_known_devices(blob.data(), blob_size, &loaded)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    known_devices_ = loaded;
    return ESP_OK;
}

esp_err_t MultipanService::persist_known_devices() noexcept {
    std::array<std::uint8_t, kZigbeeKnownDevicesBlobCapacity> blob{};
    std::size_t blob_size = 0;
    if (!encode_zigbee_known_devices(known_devices_, &blob, &blob_size)) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(
        kStoragePartition, kKnownDevicesNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_blob(handle, kKnownDevicesKey, blob.data(), blob_size);
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

esp_err_t MultipanService::remember_known_short_address(
    std::uint16_t short_address) noexcept {
    if (known_devices_.contains(short_address)) {
        return ESP_OK;
    }
    if (!known_devices_.add(short_address)) {
        return short_address == 0 || short_address == 0xFFFFU
            ? ESP_ERR_INVALID_ARG : ESP_ERR_NO_MEM;
    }
    const esp_err_t status = persist_known_devices();
    current_.zigbee_short_addresses = known_devices_.addresses;
    current_.zigbee_short_address_count = known_devices_.count;
    current_.zigbee_device_count = known_devices_.count;
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "Failed to persist Zigbee short address 0x%04x: %s",
                 static_cast<unsigned>(short_address), esp_err_to_name(status));
    } else {
        ESP_LOGI(kTag, "Persisted known Zigbee short address: 0x%04x count=%u",
                 static_cast<unsigned>(short_address),
                 static_cast<unsigned>(known_devices_.count));
    }
    return status;
}

void MultipanService::start_next_restored_interview() noexcept {
    if (!restoring_known_devices_ || restored_interview_address_ != 0) {
        return;
    }
    if (restored_interview_cursor_ >= known_devices_.count) {
        restoring_known_devices_ = false;
        restored_interview_address_ = 0;
        ESP_LOGI(kTag, "Restored device interviews complete: count=%u",
                 static_cast<unsigned>(known_devices_.count));
        return;
    }
    const std::uint16_t address =
        known_devices_.addresses[restored_interview_cursor_];
    restored_interview_address_ = address;
    ESP_LOGI(kTag, "Restoring device interview: short=0x%04x index=%u/%u",
             static_cast<unsigned>(address),
             static_cast<unsigned>(restored_interview_cursor_ + 1U),
             static_cast<unsigned>(known_devices_.count));
    start_device_interview(address);
}

void MultipanService::finish_restored_interview(
    std::uint16_t short_address) noexcept {
    if (!restoring_known_devices_ ||
        short_address != restored_interview_address_) {
        return;
    }
    ++restored_interview_cursor_;
    restored_interview_address_ = 0;
}

esp_err_t MultipanService::request_device_command(
    const ZigbeeDeviceCommand& command) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)command;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!started_ || command_queue_ == nullptr ||
        command.short_address == 0 || command.short_address == 0xFFFFU ||
        command.endpoint_id == 0 ||
        (command.kind == ZigbeeDeviceCommandKind::set_on_off &&
         command.value > 1) ||
        (command.kind == ZigbeeDeviceCommandKind::set_level &&
         command.value > 254)) {
        return ESP_ERR_INVALID_ARG;
    }
    Command queued{};
    queued.type = CommandType::DeviceCommand;
    queued.short_address = command.short_address;
    queued.endpoint_id = command.endpoint_id;
    queued.device_command_kind = command.kind;
    queued.value = command.value;
    return xQueueSend(command_queue_, &queued, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
#endif
}

bool MultipanService::read_snapshot(MultipanSnapshot* snapshot) const noexcept {
    if (snapshot == nullptr || snapshot_mutex_ == nullptr ||
        xSemaphoreTake(snapshot_mutex_, 0) != pdTRUE) {
        return false;
    }
    *snapshot = published_;
    xSemaphoreGive(snapshot_mutex_);
    return true;
}

bool MultipanService::read_snapshot(
    ZigbeeCoordinatorSnapshot* snapshot) const noexcept {
    if (snapshot == nullptr) {
        return false;
    }
    MultipanSnapshot radio_snapshot{};
    if (!read_snapshot(&radio_snapshot)) {
        return false;
    }
    snapshot->available = radio_snapshot.state == MultipanState::running;
    snapshot->stack_ready = radio_snapshot.zigbee_started &&
                            radio_snapshot.zigbee_bdb_ready;
    snapshot->network_formed = radio_snapshot.zigbee_network_formed;
    snapshot->search_pending = radio_snapshot.network_start_requested &&
                               !radio_snapshot.zigbee_network_formed &&
                               !radio_snapshot.zigbee_formation_timed_out;
    snapshot->formation_timed_out = radio_snapshot.zigbee_formation_timed_out;
    snapshot->device_count = radio_snapshot.zigbee_device_count;
    snapshot->short_addresses = radio_snapshot.zigbee_short_addresses;
    snapshot->short_address_count = radio_snapshot.zigbee_short_address_count;
    snapshot->short_address_details_available = kH2ZigbeeRcpEnabled;
    snapshot->short_addresses_truncated =
        radio_snapshot.zigbee_short_addresses_truncated;
    snapshot->device_capabilities_available = kH2ZigbeeRcpEnabled;
    snapshot->device_capabilities_truncated =
        radio_snapshot.zigbee_device_capabilities_truncated;
    snapshot->pairing_seconds_remaining =
        radio_snapshot.pairing_seconds_remaining;
    snapshot->last_error = radio_snapshot.last_error;
    snapshot->last_signal_type = radio_snapshot.last_signal_type;
    snapshot->last_bdb_status = radio_snapshot.last_bdb_status;
    snapshot->last_bdb_result = radio_snapshot.last_bdb_result;
    snapshot->last_bdb_stage = radio_snapshot.last_bdb_stage;
    return true;
}

void MultipanService::owner_task(void* context) {
    static_cast<MultipanService*>(context)->run();
}

#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
void MultipanService::zigbee_task(void* context) {
    auto* service = static_cast<MultipanService*>(context);
    const auto report_failure = [service](esp_err_t error) {
        StackEvent event{};
        event.type = EventType::Failure;
        event.error = error;
        (void)service->enqueue_stack_event(event);
    };
    esp_zigbee_config_t config{};
    config.device_config.device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR;
    config.device_config.install_code_policy = false;
    config.device_config.zczr_config.max_children = 16;
    config.platform_config.storage_partition_name = kStoragePartition;
    config.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_UART_RCP;
 #if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    config.platform_config.radio_config.radio_uart_config.port =
        static_cast<uart_port_t>(CONFIG_HUB_H2_ZIGBEE_UART_PORT);
    config.platform_config.radio_config.radio_uart_config.uart_config =
        make_multipan_uart_config();
    config.platform_config.radio_config.radio_uart_config.rx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_H2_ZIGBEE_UART_RX_GPIO);
    config.platform_config.radio_config.radio_uart_config.tx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_H2_ZIGBEE_UART_TX_GPIO);
#else
    config.platform_config.radio_config.radio_uart_config.port =
        static_cast<uart_port_t>(CONFIG_HUB_MULTIPAN_UART_PORT);
    config.platform_config.radio_config.radio_uart_config.uart_config =
        make_multipan_uart_config();
    config.platform_config.radio_config.radio_uart_config.rx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_RX_GPIO);
    config.platform_config.radio_config.radio_uart_config.tx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_TX_GPIO);
#endif

#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    esp_radio_spinel_register_rcp_failure_handler(
        spinel_reset_failure_handler, ESP_RADIO_SPINEL_ZIGBEE);
    esp_radio_spinel_set_coprocessor_reset_failure_callback(
        spinel_reset_failure_handler);
#endif
    esp_err_t status = esp_zigbee_init(&config);
    if (status != ESP_OK) {
        report_failure(status);
        vTaskDelete(nullptr);
    }
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);

    const ezb_err_t primary_status =
        ezb_bdb_set_primary_channel_set(1U << 13);
    const ezb_err_t secondary_status =
        ezb_bdb_set_secondary_channel_set(0x07FFF800U);
    if (primary_status != EZB_ERR_NONE || secondary_status != EZB_ERR_NONE ||
        ezb_app_signal_add_handler(zigbee_signal_handler) != ESP_OK) {
        report_failure(ESP_FAIL);
        vTaskDelete(nullptr);
    }

    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_zha_custom_gateway_config_t gateway_config =
        EZB_ZHA_CUSTOM_GATEWAY_CONFIG();
    ezb_af_ep_desc_t endpoint = ezb_zha_create_custom_gateway(
        1, &gateway_config);
    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(
        endpoint, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    if (device == nullptr || endpoint == nullptr || basic == nullptr ||
        ezb_zcl_basic_cluster_desc_add_attr(
            basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
            const_cast<char*>(kManufacturer)) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(
            basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
            const_cast<char*>(kModel)) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||
        ezb_af_device_desc_register(device) != EZB_ERR_NONE) {
        report_failure(ESP_FAIL);
        vTaskDelete(nullptr);
    }

    status = esp_zigbee_start(false);
    if (status != ESP_OK) {
        report_failure(status);
        vTaskDelete(nullptr);
    }
    StackEvent started_event{};
    started_event.type = EventType::ZigbeeStarted;
    started_event.error = ESP_OK;
    (void)service->enqueue_stack_event(started_event);

    (void)esp_zigbee_launch_mainloop();
    vTaskDelete(nullptr);
}

#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
void MultipanService::spinel_reset_failure_handler() {
    MultipanService* service = active_instance_;
    if (service == nullptr) {
        return;
    }
    ESP_LOGE(kTag, "H2 RCP did not respond; no hardware reset line is configured");
    StackEvent event{};
    event.type = EventType::Failure;
    event.error = ESP_ERR_TIMEOUT;
    (void)service->enqueue_stack_event(event);
}
#endif

bool MultipanService::zigbee_signal_handler(const ezb_app_signal_t* signal) {
    if (active_instance_ == nullptr || signal == nullptr) {
        return false;
    }

    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    if (type == EZB_ZDO_SIGNAL_SKIP_STARTUP) {
        StackEvent event{};
        event.type = EventType::BdbStartup;
        event.error = ESP_OK;
        event.signal_type = static_cast<std::uint16_t>(type);
        return active_instance_->enqueue_stack_event(event);
    }

    if (type == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const auto* params = static_cast<const ezb_zdo_signal_device_annce_params_t*>(
            ezb_app_signal_get_params(signal));
        if (params == nullptr) {
            return false;
        }
        StackEvent event{};
        event.type = EventType::DeviceAnnounced;
        event.short_address = static_cast<std::uint16_t>(params->short_addr);
        event.error = ESP_OK;
        return active_instance_->enqueue_stack_event(event);
    }

    if (type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ||
        type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
        const auto* status = static_cast<const ezb_bdb_comm_status_t*>(
            ezb_app_signal_get_params(signal));
        StackEvent event{};
        event.type = EventType::BdbDeviceStart;
        event.error = ESP_OK;
        event.signal_type = static_cast<std::uint16_t>(type);
        event.has_bdb_status = status != nullptr;
        event.value = status == nullptr ? 0 : static_cast<std::uint8_t>(*status);
        event.factory_new = ezb_bdb_is_factory_new();
        event.network_joined = ezb_bdb_dev_joined();
        return active_instance_->enqueue_stack_event(event);
    }

    if (type == EZB_BDB_SIGNAL_FORMATION) {
        const auto* status = static_cast<const ezb_bdb_comm_status_t*>(
            ezb_app_signal_get_params(signal));
        const bool formed = status != nullptr &&
                            *status == EZB_BDB_STATUS_SUCCESS;
        StackEvent event{};
        event.type = EventType::NetworkFormed;
        event.value = static_cast<std::uint8_t>(
            status == nullptr ? 0U : static_cast<unsigned>(*status));
        event.error = formed ? ESP_OK : ESP_FAIL;
        event.signal_type = static_cast<std::uint16_t>(type);
        event.has_bdb_status = status != nullptr;
        return active_instance_->enqueue_stack_event(event);
    }

    if (type == EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS) {
        const auto* duration = static_cast<const std::uint8_t*>(
            ezb_app_signal_get_params(signal));
        if (duration != nullptr) {
            StackEvent event{};
            event.type = EventType::PairingWindowChanged;
            event.value = *duration;
            event.error = ESP_OK;
            event.signal_type = static_cast<std::uint16_t>(type);
            (void)active_instance_->enqueue_stack_event(event);
        }
        return true;
    }

    return true;
}

void MultipanService::active_endpoints_callback(
    const ezb_zdo_active_ep_req_result_t* result, void* context) {
    auto* interview = static_cast<InterviewContext*>(context);
    if (interview == nullptr || interview->service == nullptr) {
        return;
    }
    StackEvent event{};
    event.type = EventType::ActiveEndpoints;
    event.short_address = interview->short_address;
    if (result == nullptr || result->error != EZB_ERR_NONE ||
        result->rsp == nullptr ||
        result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        event.error = result == nullptr || result->error == EZB_ERR_NONE
            ? ESP_FAIL : esp_zigbee_err_to_esp(result->error);
        ESP_LOGW(kTag, "ZDO active-endpoint response failed: short=0x%04x raw=%d status=%d mapped=%s",
                 static_cast<unsigned>(interview->short_address),
                 result == nullptr ? -1 : static_cast<int>(result->error),
                 result == nullptr || result->rsp == nullptr
                     ? -1 : static_cast<int>(result->rsp->status),
                 esp_err_to_name(event.error));
        (void)interview->service->enqueue_stack_event(event);
        return;
    }
    const std::size_t count = std::min<std::size_t>(
        result->rsp->active_ep_count, event.endpoint_ids.size());
    event.endpoint_count = static_cast<std::uint8_t>(count);
    event.truncated = result->rsp->active_ep_count > count;
    ESP_LOGI(kTag, "ZDO active endpoints: short=0x%04x count=%u truncated=%s",
             static_cast<unsigned>(interview->short_address),
             static_cast<unsigned>(result->rsp->active_ep_count),
             event.truncated ? "yes" : "no");
    if (result->rsp->active_ep_count > 0 &&
        result->rsp->active_ep_list == nullptr) {
        event.error = ESP_FAIL;
        (void)interview->service->enqueue_stack_event(event);
        return;
    }
    if (result->rsp->active_ep_list != nullptr) {
        std::copy_n(result->rsp->active_ep_list, count,
                    event.endpoint_ids.begin());
    }
    (void)interview->service->enqueue_stack_event(event);
}

void MultipanService::simple_descriptor_callback(
    const ezb_zdo_simple_desc_req_result_t* result, void* context) {
    auto* interview = static_cast<InterviewContext*>(context);
    if (interview == nullptr || interview->service == nullptr) {
        return;
    }
    StackEvent event{};
    event.type = EventType::SimpleDescriptor;
    event.short_address = interview->short_address;
    event.endpoint_id = interview->endpoint_id;
    if (result == nullptr || result->error != EZB_ERR_NONE ||
        result->rsp == nullptr || result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        event.error = result == nullptr || result->error == EZB_ERR_NONE
            ? ESP_FAIL : esp_zigbee_err_to_esp(result->error);
        ESP_LOGW(kTag, "ZDO simple-descriptor response failed: short=0x%04x ep=%u raw=%d status=%d mapped=%s",
                 static_cast<unsigned>(interview->short_address),
                 static_cast<unsigned>(interview->endpoint_id),
                 result == nullptr ? -1 : static_cast<int>(result->error),
                 result == nullptr || result->rsp == nullptr
                     ? -1 : static_cast<int>(result->rsp->status),
                 esp_err_to_name(event.error));
        (void)interview->service->enqueue_stack_event(event);
        return;
    }
    const auto& descriptor = result->rsp->desc;
    event.profile_id = descriptor.app_profile_id;
    event.device_id = descriptor.app_device_id;
    const std::size_t input_count = std::min<std::size_t>(
        descriptor.app_input_cluster_count, event.input_clusters.size());
    const std::size_t output_count = std::min<std::size_t>(
        descriptor.app_output_cluster_count, event.output_clusters.size());
    if ((descriptor.app_input_cluster_count != 0 ||
         descriptor.app_output_cluster_count != 0) &&
        descriptor.app_cluster_list == nullptr) {
        event.error = ESP_FAIL;
        (void)interview->service->enqueue_stack_event(event);
        return;
    }
    event.input_cluster_count = static_cast<std::uint8_t>(input_count);
    event.output_cluster_count = static_cast<std::uint8_t>(output_count);
    event.truncated = input_count < descriptor.app_input_cluster_count ||
                      output_count < descriptor.app_output_cluster_count;
    ESP_LOGI(kTag, "ZDO simple descriptor: short=0x%04x ep=%u profile=0x%04x device=0x%04x in=%u out=%u truncated=%s",
             static_cast<unsigned>(interview->short_address),
             static_cast<unsigned>(descriptor.ep_id),
             static_cast<unsigned>(descriptor.app_profile_id),
             static_cast<unsigned>(descriptor.app_device_id),
             static_cast<unsigned>(descriptor.app_input_cluster_count),
             static_cast<unsigned>(descriptor.app_output_cluster_count),
             event.truncated ? "yes" : "no");
    if (descriptor.app_cluster_list != nullptr) {
        std::copy_n(descriptor.app_cluster_list, input_count,
                    event.input_clusters.begin());
        std::copy_n(descriptor.app_cluster_list +
                        descriptor.app_input_cluster_count,
                    output_count, event.output_clusters.begin());
    }
    (void)interview->service->enqueue_stack_event(event);
}

void MultipanService::zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void* message) {
    MultipanService* service = active_instance_;
    if (service == nullptr || message == nullptr) {
        return;
    }

    if (callback_id == EZB_ZCL_CORE_DEFAULT_RSP_CB_ID) {
        const auto* response =
            static_cast<const ezb_zcl_cmd_default_rsp_message_t*>(message);
        if (response->in.header == nullptr) {
            return;
        }
        StackEvent event{};
        event.type = EventType::DefaultResponse;
        event.short_address = static_cast<std::uint16_t>(
            response->in.header->src_addr.u.short_addr);
        event.endpoint_id = response->in.header->src_ep;
        event.cluster_id = response->info.cluster_id;
        event.value = response->in.rsp_to_cmd;
        event.attribute_status = response->in.status_code;
        event.error = response->in.status_code == EZB_ZCL_STATUS_SUCCESS
            ? ESP_OK : ESP_FAIL;
        (void)service->enqueue_stack_event(event);
        return;
    }

    if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {
        const auto* response =
            static_cast<const ezb_zcl_cmd_read_attr_rsp_message_t*>(message);
        const bool is_on_off =
            response->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF;
        const bool is_level =
            response->info.cluster_id == EZB_ZCL_CLUSTER_ID_LEVEL;
        if ((!is_on_off && !is_level) || response->in.header == nullptr) {
            return;
        }
        StackEvent event{};
        event.type = is_level ? EventType::LevelAttribute
                              : EventType::OnOffAttribute;
        event.short_address = static_cast<std::uint16_t>(
            response->in.header->src_addr.u.short_addr);
        event.endpoint_id = response->in.header->src_ep;
        event.cluster_id = response->info.cluster_id;
        event.error = response->info.status == EZB_ZCL_STATUS_SUCCESS
            ? ESP_ERR_INVALID_RESPONSE : ESP_FAIL;
        auto* variable = response->in.variables;
        const std::uint16_t target_attribute = is_level
            ? kZigbeeLevelCurrentAttributeId
            : kZigbeeOnOffStateAttributeId;
        for (std::size_t count = 0; variable != nullptr && count < 8;
             ++count, variable = variable->next) {
            if (variable->attr_id != target_attribute) {
                continue;
            }
            event.attribute_id = variable->attr_id;
            event.attribute_type = variable->attr_type;
            event.attribute_status = variable->status;
            if (response->info.status == EZB_ZCL_STATUS_SUCCESS &&
                variable->status == EZB_ZCL_STATUS_SUCCESS &&
                variable->attr_value != nullptr) {
                event.value = *static_cast<const std::uint8_t*>(
                    variable->attr_value);
                event.error = ESP_OK;
            }
            break;
        }
        (void)service->enqueue_stack_event(event);
        return;
    }

    if (callback_id == EZB_ZCL_CORE_REPORT_ATTR_CB_ID) {
        const auto* report =
            static_cast<const ezb_zcl_cmd_report_attr_message_t*>(message);
        const bool is_on_off =
            report->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF;
        const bool is_level =
            report->info.cluster_id == EZB_ZCL_CLUSTER_ID_LEVEL;
        if ((!is_on_off && !is_level) || report->in.header == nullptr) {
            return;
        }
        auto* variable = report->in.variables;
        const std::uint16_t target_attribute = is_level
            ? kZigbeeLevelCurrentAttributeId
            : kZigbeeOnOffStateAttributeId;
        for (std::size_t count = 0; variable != nullptr && count < 8;
             ++count, variable = variable->next) {
            if (variable->attr_id != target_attribute) {
                continue;
            }
            StackEvent event{};
            event.type = is_level ? EventType::LevelAttribute
                                  : EventType::OnOffAttribute;
            event.short_address = static_cast<std::uint16_t>(
                report->in.header->src_addr.u.short_addr);
            event.endpoint_id = report->in.header->src_ep;
            event.cluster_id = report->info.cluster_id;
            event.attribute_id = variable->attr_id;
            event.attribute_type = variable->attr_type;
            event.attribute_status = EZB_ZCL_STATUS_SUCCESS;
            event.error = report->info.status != EZB_ZCL_STATUS_SUCCESS ||
                          variable->attr_value == nullptr
                ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
            if (variable->attr_value != nullptr) {
                event.value = *static_cast<const std::uint8_t*>(
                    variable->attr_value);
            }
            (void)service->enqueue_stack_event(event);
            return;
        }
    }
}
#endif

void MultipanService::run() noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    vTaskDelete(nullptr);
#else
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    const esp_vfs_eventfd_config_t eventfd_config = {.max_fds = 2};
    esp_err_t status = esp_vfs_eventfd_register(&eventfd_config);
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        fail(status);
        vTaskDelete(nullptr);
    }
    status = xTaskCreate(zigbee_task, "zigbee_h2_rcp", 8192, this, 5,
                         &zigbee_task_) == pdPASS
                 ? ESP_OK
                 : ESP_ERR_NO_MEM;
    if (status != ESP_OK) {
        fail(status);
        vTaskDelete(nullptr);
    }
    current_.state = MultipanState::running;
    current_.last_error = ESP_OK;
    publish();
#else
    wifi::ProvisioningUpdate wifi_snapshot{};
    esp_netif_t* backhaul = nullptr;
    TickType_t wait_start = xTaskGetTickCount();
    while ((backhaul = provisioning_.station_netif()) == nullptr ||
           !provisioning_.read_snapshot(&wifi_snapshot) ||
           !wifi_snapshot.credential_store_ready) {
        if (xTaskGetTickCount() - wait_start >= pdMS_TO_TICKS(kProvisioningWaitMs)) {
            current_.state = MultipanState::failed;
            current_.last_error = ESP_ERR_TIMEOUT;
            publish();
            vTaskDelete(nullptr);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_vfs_eventfd_config_t eventfd_config = {.max_fds = 8};
    esp_err_t status = esp_vfs_eventfd_register(&eventfd_config);
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        fail(status);
        vTaskDelete(nullptr);
    }
    status = mdns_init();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        fail(status);
        vTaskDelete(nullptr);
    }
    status = mdns_hostname_set("p4-multiprotocol-hub");
    if (status != ESP_OK) {
        fail(status);
        vTaskDelete(nullptr);
    }

    const esp_radio_spinel_multipan_radio_config_t radio_config = {
        .radio_mode = RADIO_MODE_UART_RCP,
        .radio_uart_config = {
            .port = static_cast<uart_port_t>(CONFIG_HUB_MULTIPAN_UART_PORT),
            .uart_config = make_multipan_uart_config(),
            .rx_pin = static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_RX_GPIO),
            .tx_pin = static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_TX_GPIO),
        },
    };
    status = esp_radio_spinel_multipan_init(&radio_config);
    if (status != ESP_OK) {
        fail(status);
        vTaskDelete(nullptr);
    }

    status = xTaskCreate(zigbee_task, "zigbee_multipan", 8192, this, 5,
                         &zigbee_task_) == pdPASS
                 ? ESP_OK
                 : ESP_ERR_NO_MEM;
    if (status != ESP_OK) {
        fail(status);
        vTaskDelete(nullptr);
    }

    esp_openthread_set_backbone_netif(backhaul);
    esp_openthread_config_t thread_config{};
    thread_config.netif_config = ESP_NETIF_DEFAULT_OPENTHREAD();
    thread_config.platform_config.radio_config.radio_mode = RADIO_MODE_UART_RCP;
    thread_config.platform_config.radio_config.radio_uart_config.port =
        static_cast<uart_port_t>(CONFIG_HUB_MULTIPAN_UART_PORT);
    thread_config.platform_config.radio_config.radio_uart_config.uart_config =
        make_multipan_uart_config();
    thread_config.platform_config.radio_config.radio_uart_config.rx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_RX_GPIO);
    thread_config.platform_config.radio_config.radio_uart_config.tx_pin =
        static_cast<gpio_num_t>(CONFIG_HUB_MULTIPAN_UART_TX_GPIO);
    thread_config.platform_config.host_config.host_connection_mode =
        HOST_CONNECTION_MODE_NONE;
    thread_config.platform_config.port_config.storage_partition_name = kStoragePartition;
    thread_config.platform_config.port_config.netif_queue_size = 10;
    thread_config.platform_config.port_config.task_queue_size = 10;
    status = esp_openthread_start(&thread_config);
    if (status != ESP_OK) {
        fail(status);
        vTaskDelete(nullptr);
    }

    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
        otInstance* instance = esp_openthread_get_instance();
        otOperationalDatasetTlvs active_dataset{};
        if (instance != nullptr &&
            otDatasetGetActiveTlvs(instance, &active_dataset) == OT_ERROR_NONE) {
            status = esp_openthread_auto_start(&active_dataset);
            if (status != ESP_OK) {
                ESP_LOGW(kTag, "Stored Thread dataset could not be resumed: %s",
                         esp_err_to_name(status));
            }
        }
        esp_openthread_lock_release();
    }

    current_.state = MultipanState::running;
    current_.last_error = ESP_OK;
    publish();
#endif

    TickType_t last_status_refresh = 0;
    for (;;) {
        Command command{};
        if (xQueueReceive(command_queue_, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_command(command);
        }

        StackEvent event{};
        while (xQueueReceive(stack_event_queue_, &event, 0) == pdTRUE) {
            handle_stack_event(event);
        }
        if (restoring_known_devices_ && restored_interview_address_ == 0) {
            start_next_restored_interview();
        }

        const TickType_t now = xTaskGetTickCount();
        if (pairing_deadline_ > now) {
            current_.pairing_seconds_remaining = static_cast<std::uint8_t>(
                (pairing_deadline_ - now + configTICK_RATE_HZ - 1) /
                configTICK_RATE_HZ);
        } else {
            current_.pairing_seconds_remaining = 0;
            pairing_deadline_ = 0;
        }

        if (now - last_status_refresh >= pdMS_TO_TICKS(1000)) {
#if CONFIG_HUB_MULTIPAN_ENABLE
            refresh_thread_status();
#endif
            last_status_refresh = now;
            publish();
        }

        if (formation_deadline_ != 0 &&
            static_cast<std::int32_t>(now - formation_deadline_) >= 0) {
            if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
                current_.last_bdb_status = static_cast<std::uint8_t>(
                    ezb_bdb_get_commissioning_status());
                esp_zigbee_lock_release();
            }
            current_.last_bdb_stage = kBdbStageTimeout;
            current_.last_error = ESP_ERR_TIMEOUT;
            const auto outcome = finish_zigbee_formation(false, true, false);
            current_.zigbee_network_formed = outcome.network_formed;
            current_.zigbee_formation_timed_out = outcome.formation_timed_out;
            current_.network_start_requested = outcome.search_pending;
            formation_deadline_ = 0;
            open_join_after_formation_ = false;
            ESP_LOGE(kTag, "BDB formation timed out; status=%u",
                     static_cast<unsigned>(current_.last_bdb_status));
            publish();
        }

        bool interview_timed_out = false;
        std::uint16_t restored_timeout_address = 0;
        for (std::size_t index = 0;
             index < current_device_capability_count_; ++index) {
            if (interview_deadlines_[index] == 0 ||
                static_cast<std::int32_t>(now - interview_deadlines_[index]) < 0) {
                continue;
            }
            auto& device = current_device_capabilities_[index];
            device.interview_pending = false;
            device.interview_complete = true;
            device.interview_failed = true;
            device.interview_error = ESP_ERR_TIMEOUT;
            interview_deadlines_[index] = 0;
            interview_timed_out = true;
            if (restoring_known_devices_ &&
                device.short_address == restored_interview_address_) {
                restored_timeout_address = device.short_address;
            }
            ESP_LOGW(kTag, "ZDO interview timed out: short=0x%04x",
                     static_cast<unsigned>(device.short_address));
        }
        if (interview_timed_out) {
            publish();
            if (restored_timeout_address != 0) {
                finish_restored_interview(restored_timeout_address);
            }
        }

        bool on_off_read_timed_out = false;
        bool level_read_timed_out = false;
        for (std::size_t device_index = 0;
             device_index < current_device_capability_count_; ++device_index) {
            auto& device = current_device_capabilities_[device_index];
            for (std::size_t endpoint_index = 0;
                 endpoint_index < device.endpoint_count; ++endpoint_index) {
                auto& deadline = on_off_read_deadlines_[device_index][endpoint_index];
                if (deadline != 0 &&
                    static_cast<std::int32_t>(now - deadline) >= 0) {
                    fail_zigbee_on_off_read(
                        device.endpoints[endpoint_index].on_off_state);
                    deadline = 0;
                    on_off_read_timed_out = true;
                    ESP_LOGW(kTag,
                             "OnOff read timed out: short=0x%04x ep=%u",
                             static_cast<unsigned>(device.short_address),
                             static_cast<unsigned>(
                                 device.endpoints[endpoint_index].endpoint_id));
                }
            auto& level_deadline =
                level_read_deadlines_[device_index][endpoint_index];
            if (level_deadline != 0 &&
                static_cast<std::int32_t>(now - level_deadline) >= 0) {
                fail_zigbee_level_read(
                    device.endpoints[endpoint_index].level_state);
                level_deadline = 0;
                level_read_timed_out = true;
                ESP_LOGW(kTag,
                         "Level read timed out: short=0x%04x ep=%u",
                         static_cast<unsigned>(device.short_address),
                         static_cast<unsigned>(
                             device.endpoints[endpoint_index].endpoint_id));
            }
            }
        }
        if (on_off_read_timed_out || level_read_timed_out) {
            publish();
        }
    }
#endif
}

void MultipanService::handle_stack_event(const StackEvent& event) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)event;
#else
    bool open_join_window = false;
    bool begin_device_interview = false;
    bool begin_restored_interview = false;
    bool advance_restored_interview = false;
    bool request_next_descriptor = false;
    std::uint16_t interview_address = 0;
    std::uint8_t interview_endpoint = 0;
    switch (event.type) {
        case EventType::ZigbeeStarted:
            current_.zigbee_started = event.error == ESP_OK;
            break;
        case EventType::BdbStartup: {
            current_.last_signal_type = event.signal_type;
            current_.last_bdb_stage = kBdbStageInitialization;
            if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
                current_.last_error = ESP_ERR_TIMEOUT;
                break;
            }
            const ezb_err_t result = ezb_bdb_start_top_level_commissioning(
                EZB_BDB_MODE_INITIALIZATION);
            esp_zigbee_lock_release();
            current_.last_bdb_result = static_cast<std::int32_t>(result);
            current_.last_error = esp_zigbee_err_to_esp(result);
            if (result == EZB_ERR_NONE) {
                current_.last_bdb_status = EZB_BDB_STATUS_IN_PROGRESS;
            }
            ESP_LOGI(kTag, "BDB initialization returned raw=%d mapped=%s",
                     static_cast<int>(result),
                     esp_err_to_name(current_.last_error));
            break;
        }
        case EventType::BdbDeviceStart:
            current_.last_signal_type = event.signal_type;
            if (event.has_bdb_status) {
                current_.last_bdb_status = event.value;
            }
            if (!event.has_bdb_status ||
                event.value != EZB_BDB_STATUS_SUCCESS) {
                current_.zigbee_bdb_ready = false;
                current_.last_error = ESP_FAIL;
                current_.network_start_requested = false;
            } else {
                current_.zigbee_bdb_ready = true;
                current_.last_error = ESP_OK;
                current_.zigbee_network_formed = false;
                if (event.factory_new) {
                    known_devices_ = {};
                    current_.zigbee_short_addresses = {};
                    current_.zigbee_short_address_count = 0;
                    current_.zigbee_short_addresses_truncated = false;
                    current_.zigbee_device_count = 0;
                    current_device_capabilities_ = {};
                    current_device_capability_count_ = 0;
                    interview_next_endpoint_ = {};
                    interview_deadlines_ = {};
                    on_off_read_deadlines_ = {};
                    level_read_deadlines_ = {};
                    restoring_known_devices_ = false;
                    restored_interview_cursor_ = 0;
                    restored_interview_address_ = 0;
                    const esp_err_t clear_status = persist_known_devices();
                    if (clear_status != ESP_OK) {
                        ESP_LOGE(kTag,
                                 "Failed to clear old known Zigbee addresses: %s",
                                 esp_err_to_name(clear_status));
                    }
                }
                if (!event.factory_new && event.network_joined) {
                    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
                        current_.last_error = ESP_ERR_TIMEOUT;
                    } else {
                        const ezb_err_t result = ezb_bdb_close_network();
                        esp_zigbee_lock_release();
                        current_.last_bdb_result =
                            static_cast<std::int32_t>(result);
                        current_.last_bdb_stage = kBdbStageCloseNetwork;
                        current_.last_error = esp_zigbee_err_to_esp(result);
                        current_.zigbee_network_formed = result == EZB_ERR_NONE;
                    }
                    if (current_.zigbee_network_formed) {
                        const esp_err_t load_status = load_known_devices();
                        if (load_status != ESP_OK) {
                            ESP_LOGE(kTag,
                                     "Known Zigbee address store is invalid: %s",
                                     esp_err_to_name(load_status));
                        } else {
                            current_.zigbee_short_addresses =
                                known_devices_.addresses;
                            current_.zigbee_short_address_count =
                                known_devices_.count;
                            current_.zigbee_device_count =
                                known_devices_.count;
                            current_.zigbee_short_addresses_truncated = false;
                            current_device_capabilities_ = {};
                            current_device_capability_count_ =
                                known_devices_.count;
                            interview_next_endpoint_ = {};
                            interview_deadlines_ = {};
                            on_off_read_deadlines_ = {};
                            level_read_deadlines_ = {};
                            for (std::size_t index = 0;
                                 index < known_devices_.count; ++index) {
                                auto& device =
                                    current_device_capabilities_[index];
                                device.short_address =
                                    known_devices_.addresses[index];
                                device.interview_pending = true;
                            }
                            restored_interview_cursor_ = 0;
                            restoring_known_devices_ =
                                known_devices_.count != 0;
                            begin_restored_interview =
                                restoring_known_devices_;
                            ESP_LOGI(kTag,
                                     "Restored known Zigbee short addresses: count=%u",
                                     static_cast<unsigned>(known_devices_.count));
                        }
                    }
                }
            }
            ESP_LOGI(kTag,
                     "BDB device-start signal: status=%u factory_new=%s joined=%s ready=%s",
                     static_cast<unsigned>(current_.last_bdb_status),
                     event.factory_new ? "yes" : "no",
                     event.network_joined ? "yes" : "no",
                     current_.zigbee_bdb_ready ? "yes" : "no");
            break;
        case EventType::NetworkFormed: {
            current_.last_signal_type = event.signal_type;
            current_.last_bdb_stage = kBdbStageFormationSignal;
            if (event.has_bdb_status) {
                current_.last_bdb_status = event.value;
            }
            current_.last_error = event.error;
            if (event.error == ESP_OK) {
                if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
                    current_.last_error = ESP_ERR_TIMEOUT;
                } else {
                    const ezb_err_t result = ezb_bdb_close_network();
                    esp_zigbee_lock_release();
                    current_.last_bdb_result = static_cast<std::int32_t>(result);
                    current_.last_bdb_stage = kBdbStageCloseNetwork;
                    current_.last_error = esp_zigbee_err_to_esp(result);
                }
            }
            const auto outcome = finish_zigbee_formation(
                current_.last_error == ESP_OK, false,
                open_join_after_formation_);
            current_.zigbee_network_formed = outcome.network_formed;
            current_.network_start_requested = outcome.search_pending;
            current_.zigbee_formation_timed_out = outcome.formation_timed_out;
            formation_deadline_ = 0;
            open_join_window = outcome.open_join_window;
            open_join_after_formation_ = false;
            break;
        }
        case EventType::DeviceAnnounced: {
            const esp_err_t remember_status =
                remember_known_short_address(event.short_address);
            if (remember_status == ESP_ERR_INVALID_ARG ||
                remember_status == ESP_ERR_NO_MEM) {
                ++current_.dropped_announcement_events;
                current_.zigbee_short_addresses_truncated =
                    known_devices_.count >= known_devices_.addresses.size();
                current_.zigbee_device_capabilities_truncated =
                    known_devices_.count >= known_devices_.addresses.size();
                break;
            }
            const auto device_end = current_device_capabilities_.begin() +
                current_device_capability_count_;
            const auto device = std::find_if(
                current_device_capabilities_.begin(), device_end,
                [&event](const ZigbeeDeviceCapabilities& item) {
                    return item.short_address == event.short_address;
                });
            if (device == device_end &&
                current_device_capability_count_ <
                    current_device_capabilities_.size()) {
                auto& new_device = current_device_capabilities_[
                    current_device_capability_count_++];
                new_device = {};
                new_device.short_address = event.short_address;
                new_device.interview_pending = true;
                if (!restoring_known_devices_) {
                    begin_device_interview = true;
                    interview_address = event.short_address;
                }
            } else if (device == device_end) {
                current_.zigbee_device_capabilities_truncated = true;
            }
            break;
        }
        case EventType::ActiveEndpoints: {
            auto device = std::find_if(
                current_device_capabilities_.begin(),
                current_device_capabilities_.begin() +
                    current_device_capability_count_,
                [&event](const ZigbeeDeviceCapabilities& item) {
                    return item.short_address == event.short_address;
                });
            if (device == current_device_capabilities_.begin() +
                              current_device_capability_count_) {
                break;
            }
            if (event.error != ESP_OK || event.endpoint_count == 0) {
                const std::size_t device_index = static_cast<std::size_t>(
                    device - current_device_capabilities_.begin());
                interview_deadlines_[device_index] = 0;
                device->interview_pending = false;
                device->interview_complete = true;
                device->interview_failed = true;
                device->interview_error = event.error == ESP_OK
                    ? ESP_ERR_NOT_FOUND : event.error;
                advance_restored_interview = true;
                break;
            }
            device->endpoint_count = event.endpoint_count;
            for (std::size_t index = 0; index < event.endpoint_count; ++index) {
                device->endpoints[index].endpoint_id = event.endpoint_ids[index];
            }
            if (event.truncated) {
                current_.zigbee_device_capabilities_truncated = true;
            }
            const std::size_t device_index = static_cast<std::size_t>(
                device - current_device_capabilities_.begin());
            interview_deadlines_[device_index] = 0;
            interview_next_endpoint_[device_index] = 0;
            request_next_descriptor = true;
            interview_address = event.short_address;
            interview_endpoint = event.endpoint_ids[0];
            break;
        }
        case EventType::SimpleDescriptor: {
            auto device = std::find_if(
                current_device_capabilities_.begin(),
                current_device_capabilities_.begin() +
                    current_device_capability_count_,
                [&event](const ZigbeeDeviceCapabilities& item) {
                    return item.short_address == event.short_address;
                });
            if (device == current_device_capabilities_.begin() +
                              current_device_capability_count_) {
                break;
            }
            const std::size_t device_index = static_cast<std::size_t>(
                device - current_device_capabilities_.begin());
            if (event.error != ESP_OK) {
                interview_deadlines_[device_index] = 0;
                device->interview_pending = false;
                device->interview_complete = true;
                device->interview_failed = true;
                device->interview_error = event.error;
                advance_restored_interview = true;
                break;
            }
            auto endpoint = std::find_if(
                device->endpoints.begin(),
                device->endpoints.begin() + device->endpoint_count,
                [&event](const ZigbeeEndpointCapabilities& item) {
                    return item.endpoint_id == event.endpoint_id;
                });
            if (endpoint == device->endpoints.begin() + device->endpoint_count) {
                interview_deadlines_[device_index] = 0;
                device->interview_pending = false;
                device->interview_complete = true;
                device->interview_failed = true;
                device->interview_error = ESP_ERR_NOT_FOUND;
                advance_restored_interview = true;
                break;
            }
            endpoint->profile_id = event.profile_id;
            endpoint->device_id = event.device_id;
            endpoint->input_cluster_count = event.input_cluster_count;
            endpoint->output_cluster_count = event.output_cluster_count;
            endpoint->input_clusters = event.input_clusters;
            endpoint->output_clusters = event.output_clusters;
            endpoint->clusters_truncated = event.truncated;
            endpoint->on_off_state = {};
            endpoint->level_state = {};
            on_off_read_deadlines_[device_index][static_cast<std::size_t>(
                endpoint - device->endpoints.begin())] = 0;
            level_read_deadlines_[device_index][static_cast<std::size_t>(
                endpoint - device->endpoints.begin())] = 0;
            if (event.truncated) {
                current_.zigbee_device_capabilities_truncated = true;
            }
            ++interview_next_endpoint_[device_index];
            if (interview_next_endpoint_[device_index] < device->endpoint_count) {
                request_next_descriptor = true;
                interview_address = event.short_address;
                interview_endpoint = device->endpoints[
                    interview_next_endpoint_[device_index]].endpoint_id;
            } else {
                interview_deadlines_[device_index] = 0;
                device->interview_pending = false;
                device->interview_complete = true;
                device->interview_failed = false;
                device->interview_error = ESP_OK;
                advance_restored_interview = true;
            }
            break;
        }
        case EventType::OnOffAttribute: {
            auto device = std::find_if(
                current_device_capabilities_.begin(),
                current_device_capabilities_.begin() +
                    current_device_capability_count_,
                [&event](const ZigbeeDeviceCapabilities& item) {
                    return item.short_address == event.short_address;
                });
            if (device == current_device_capabilities_.begin() +
                              current_device_capability_count_) {
                break;
            }
            auto endpoint = std::find_if(
                device->endpoints.begin(),
                device->endpoints.begin() + device->endpoint_count,
                [&event](const ZigbeeEndpointCapabilities& item) {
                    return item.endpoint_id == event.endpoint_id;
                });
            if (endpoint == device->endpoints.begin() + device->endpoint_count) {
                break;
            }
            const std::size_t device_index = static_cast<std::size_t>(
                device - current_device_capabilities_.begin());
            const std::size_t endpoint_index = static_cast<std::size_t>(
                endpoint - device->endpoints.begin());
            on_off_read_deadlines_[device_index][endpoint_index] = 0;
            if (event.error != ESP_OK ||
                !apply_zigbee_on_off_attribute(
                    endpoint->on_off_state, event.cluster_id,
                    event.attribute_id, event.attribute_status,
                    event.attribute_type, event.value,
                    EZB_ZCL_STATUS_SUCCESS, EZB_ZCL_ATTR_TYPE_BOOL,
                    static_cast<std::uint32_t>(esp_timer_get_time() / 1000))) {
                fail_zigbee_on_off_read(endpoint->on_off_state);
            }
            break;
        }
        case EventType::LevelAttribute: {
            auto device = std::find_if(
                current_device_capabilities_.begin(),
                current_device_capabilities_.begin() +
                    current_device_capability_count_,
                [&event](const ZigbeeDeviceCapabilities& item) {
                    return item.short_address == event.short_address;
                });
            if (device == current_device_capabilities_.begin() +
                              current_device_capability_count_) {
                break;
            }
            auto endpoint = std::find_if(
                device->endpoints.begin(),
                device->endpoints.begin() + device->endpoint_count,
                [&event](const ZigbeeEndpointCapabilities& item) {
                    return item.endpoint_id == event.endpoint_id;
                });
            if (endpoint == device->endpoints.begin() + device->endpoint_count) {
                break;
            }
            const std::size_t device_index = static_cast<std::size_t>(
                device - current_device_capabilities_.begin());
            const std::size_t endpoint_index = static_cast<std::size_t>(
                endpoint - device->endpoints.begin());
            level_read_deadlines_[device_index][endpoint_index] = 0;
            if (event.error != ESP_OK ||
                !apply_zigbee_level_attribute(
                    endpoint->level_state, event.cluster_id,
                    event.attribute_id, event.attribute_status,
                    event.attribute_type, event.value,
                    EZB_ZCL_STATUS_SUCCESS, EZB_ZCL_ATTR_TYPE_UINT8,
                    static_cast<std::uint32_t>(esp_timer_get_time() / 1000))) {
                fail_zigbee_level_read(endpoint->level_state);
            }
            break;
        }
        case EventType::DefaultResponse:
            if (event.error == ESP_OK) {
                ESP_LOGI(kTag,
                         "ZCL default response: short=0x%04x ep=%u cluster=0x%04x cmd=0x%02x status=0x%02x",
                         static_cast<unsigned>(event.short_address),
                         static_cast<unsigned>(event.endpoint_id),
                         static_cast<unsigned>(event.cluster_id),
                         static_cast<unsigned>(event.value),
                         static_cast<unsigned>(event.attribute_status));
            } else {
                ESP_LOGW(kTag,
                         "ZCL default response failed: short=0x%04x ep=%u cluster=0x%04x cmd=0x%02x status=0x%02x",
                         static_cast<unsigned>(event.short_address),
                         static_cast<unsigned>(event.endpoint_id),
                         static_cast<unsigned>(event.cluster_id),
                         static_cast<unsigned>(event.value),
                         static_cast<unsigned>(event.attribute_status));
            }
            break;
        case EventType::PairingWindowChanged:
            current_.last_signal_type = event.signal_type;
            current_.pairing_seconds_remaining = event.value;
            if (event.value == 0) {
                pairing_deadline_ = 0;
            } else {
                pairing_deadline_ = xTaskGetTickCount() +
                    pdMS_TO_TICKS(static_cast<std::uint32_t>(event.value) * 1000U);
            }
            break;
        case EventType::Failure:
            current_.state = MultipanState::failed;
            current_.last_error = event.error;
            current_.zigbee_bdb_ready = false;
            current_.network_start_requested = false;
            formation_deadline_ = 0;
            break;
    }
    publish();
    if (begin_device_interview) {
        start_device_interview(interview_address);
    }
    if (begin_restored_interview) {
        start_next_restored_interview();
    }
    if (request_next_descriptor) {
        request_simple_descriptor(interview_address, interview_endpoint);
    }
    if (advance_restored_interview) {
        finish_restored_interview(event.short_address);
    }
    if (open_join_window) {
        handle_command({CommandType::OpenJoinWindow, kMaxJoinWindowSeconds});
    }
#endif
}

void MultipanService::start_device_interview(
    std::uint16_t short_address) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
#else
    auto device = std::find_if(
        current_device_capabilities_.begin(),
        current_device_capabilities_.begin() + current_device_capability_count_,
        [short_address](const ZigbeeDeviceCapabilities& item) {
            return item.short_address == short_address;
        });
    if (device == current_device_capabilities_.begin() +
                      current_device_capability_count_) {
        if (current_device_capability_count_ >=
            current_device_capabilities_.size()) {
            current_.zigbee_device_capabilities_truncated = true;
            publish();
            return;
        }
        device = current_device_capabilities_.begin() +
            current_device_capability_count_++;
    }
    const std::size_t index = static_cast<std::size_t>(
        device - current_device_capabilities_.begin());
    *device = {};
    device->short_address = short_address;
    device->interview_pending = true;
    interview_next_endpoint_[index] = 0;
    interview_deadlines_[index] = 0;
    publish();
    auto& context = interview_contexts_[index];
    context.service = this;
    context.short_address = short_address;

    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        device->interview_pending = false;
        device->interview_complete = true;
        device->interview_failed = true;
        device->interview_error = ESP_ERR_TIMEOUT;
        interview_deadlines_[index] = 0;
        publish();
        finish_restored_interview(short_address);
        return;
    }
    ezb_zdo_active_ep_req_t request{};
    request.dst_nwk_addr = short_address;
    request.field.nwk_addr_of_interest = short_address;
    request.cb = active_endpoints_callback;
    request.user_ctx = &context;
    const ezb_err_t result = ezb_zdo_active_ep_req(&request);
    esp_zigbee_lock_release();
    ESP_LOGI(kTag, "ZDO active-endpoint request queued: short=0x%04x raw=%d",
             static_cast<unsigned>(short_address), static_cast<int>(result));
    if (result != EZB_ERR_NONE) {
        device->interview_pending = false;
        device->interview_complete = true;
        device->interview_failed = true;
        device->interview_error = esp_zigbee_err_to_esp(result);
        interview_deadlines_[index] = 0;
        publish();
        finish_restored_interview(short_address);
    } else {
        interview_deadlines_[index] = xTaskGetTickCount() +
            pdMS_TO_TICKS(kZdoInterviewTimeoutMs);
    }
#endif
}

void MultipanService::request_simple_descriptor(
    std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    (void)endpoint_id;
#else
    auto device = std::find_if(
        current_device_capabilities_.begin(),
        current_device_capabilities_.begin() + current_device_capability_count_,
        [short_address](const ZigbeeDeviceCapabilities& item) {
            return item.short_address == short_address;
        });
    if (device == current_device_capabilities_.begin() +
                      current_device_capability_count_) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(
        device - current_device_capabilities_.begin());
    auto& context = interview_contexts_[index];
    context.service = this;
    context.short_address = short_address;
    context.endpoint_id = endpoint_id;

    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        device->interview_pending = false;
        device->interview_complete = true;
        device->interview_failed = true;
        device->interview_error = ESP_ERR_TIMEOUT;
        interview_deadlines_[index] = 0;
        publish();
        finish_restored_interview(short_address);
        return;
    }
    ezb_zdo_simple_desc_req_t request{};
    request.dst_nwk_addr = short_address;
    request.field.nwk_addr_of_interest = short_address;
    request.field.endpoint = endpoint_id;
    request.cb = simple_descriptor_callback;
    request.user_ctx = &context;
    const ezb_err_t result = ezb_zdo_simple_desc_req(&request);
    esp_zigbee_lock_release();
    ESP_LOGI(kTag, "ZDO simple-descriptor request queued: short=0x%04x ep=%u raw=%d",
             static_cast<unsigned>(short_address),
             static_cast<unsigned>(endpoint_id), static_cast<int>(result));
    if (result != EZB_ERR_NONE) {
        device->interview_pending = false;
        device->interview_complete = true;
        device->interview_failed = true;
        device->interview_error = esp_zigbee_err_to_esp(result);
        interview_deadlines_[index] = 0;
        publish();
        finish_restored_interview(short_address);
    } else {
        interview_deadlines_[index] = xTaskGetTickCount() +
            pdMS_TO_TICKS(kZdoInterviewTimeoutMs);
    }
#endif
}

void MultipanService::read_device_on_off_state(
    std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    (void)endpoint_id;
#else
    if (current_.state != MultipanState::running ||
        !current_.zigbee_network_formed) {
        return;
    }
    auto device = std::find_if(
        current_device_capabilities_.begin(),
        current_device_capabilities_.begin() + current_device_capability_count_,
        [short_address](const ZigbeeDeviceCapabilities& item) {
            return item.short_address == short_address;
        });
    if (device == current_device_capabilities_.begin() +
                      current_device_capability_count_ ||
        !device->interview_complete || device->interview_failed) {
        return;
    }
    auto endpoint = std::find_if(
        device->endpoints.begin(), device->endpoints.begin() + device->endpoint_count,
        [endpoint_id](const ZigbeeEndpointCapabilities& item) {
            return item.endpoint_id == endpoint_id;
        });
    if (endpoint == device->endpoints.begin() + device->endpoint_count ||
        std::find(endpoint->input_clusters.begin(),
                  endpoint->input_clusters.begin() +
                      endpoint->input_cluster_count,
                  kZigbeeOnOffClusterId) == endpoint->input_clusters.begin() +
                      endpoint->input_cluster_count ||
        endpoint->on_off_state.read_pending ||
        endpoint->level_state.read_pending) {
        return;
    }

    const std::size_t device_index = static_cast<std::size_t>(
        device - current_device_capabilities_.begin());
    const std::size_t endpoint_index = static_cast<std::size_t>(
        endpoint - device->endpoints.begin());
    mark_zigbee_on_off_read_pending(endpoint->on_off_state);
    on_off_read_deadlines_[device_index][endpoint_index] =
        xTaskGetTickCount() + pdMS_TO_TICKS(kZdoInterviewTimeoutMs);
    publish();

    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        fail_zigbee_on_off_read(endpoint->on_off_state);
        on_off_read_deadlines_[device_index][endpoint_index] = 0;
        publish();
        return;
    }
    std::uint16_t attribute_id = kZigbeeOnOffStateAttributeId;
    ezb_zcl_read_attr_cmd_t request{};
    ezb_address_set_short(&request.cmd_ctrl.dst_addr, short_address);
    request.cmd_ctrl.dst_ep = endpoint_id;
    request.cmd_ctrl.src_ep = 1;
    request.cmd_ctrl.cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF;
    request.cmd_ctrl.manuf_code = EZB_ZCL_STD_MANUF_CODE;
    request.cmd_ctrl.fc.manuf_specific = EZB_ZCL_NOT_MANUFACTURER_SPECIFIC;
    request.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV;
    request.cmd_ctrl.fc.dis_default_rsp =
        EZB_ZCL_FRAME_ENABLE_DEFAULT_RESPONSE;
    request.payload.attr_number = 1;
    request.payload.attr_field = &attribute_id;
    const ezb_err_t result = ezb_zcl_read_attr_cmd_req(&request);
    esp_zigbee_lock_release();
    if (result != EZB_ERR_NONE) {
        fail_zigbee_on_off_read(endpoint->on_off_state);
        on_off_read_deadlines_[device_index][endpoint_index] = 0;
        ESP_LOGW(kTag,
                 "OnOff read request failed: short=0x%04x ep=%u raw=%d",
                 static_cast<unsigned>(short_address),
                 static_cast<unsigned>(endpoint_id), static_cast<int>(result));
        publish();
        return;
    }
    ESP_LOGI(kTag, "OnOff read request queued: short=0x%04x ep=%u",
             static_cast<unsigned>(short_address),
             static_cast<unsigned>(endpoint_id));
#endif
}

void MultipanService::read_device_level_state(
    std::uint16_t short_address, std::uint8_t endpoint_id) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)short_address;
    (void)endpoint_id;
#else
    auto device = std::find_if(
        current_device_capabilities_.begin(),
        current_device_capabilities_.begin() +
            current_device_capability_count_,
        [short_address](const ZigbeeDeviceCapabilities& item) {
            return item.short_address == short_address;
        });
    if (device == current_device_capabilities_.begin() +
                  current_device_capability_count_) {
        return;
    }
    auto endpoint = std::find_if(
        device->endpoints.begin(),
        device->endpoints.begin() + device->endpoint_count,
        [endpoint_id](const ZigbeeEndpointCapabilities& item) {
            return item.endpoint_id == endpoint_id;
        });
    if (endpoint == device->endpoints.begin() + device->endpoint_count ||
        std::find(endpoint->input_clusters.begin(),
                  endpoint->input_clusters.begin() +
                      endpoint->input_cluster_count,
                  kZigbeeLevelControlClusterId) ==
            endpoint->input_clusters.begin() + endpoint->input_cluster_count ||
        endpoint->level_state.read_pending ||
        endpoint->on_off_state.read_pending) {
        return;
    }

    const std::size_t device_index = static_cast<std::size_t>(
        device - current_device_capabilities_.begin());
    const std::size_t endpoint_index = static_cast<std::size_t>(
        endpoint - device->endpoints.begin());
    mark_zigbee_level_read_pending(endpoint->level_state);
    level_read_deadlines_[device_index][endpoint_index] =
        xTaskGetTickCount() + pdMS_TO_TICKS(kZdoInterviewTimeoutMs);
    publish();

    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        fail_zigbee_level_read(endpoint->level_state);
        level_read_deadlines_[device_index][endpoint_index] = 0;
        publish();
        return;
    }
    std::uint16_t attribute_id = kZigbeeLevelCurrentAttributeId;
    ezb_zcl_read_attr_cmd_t request{};
    ezb_address_set_short(&request.cmd_ctrl.dst_addr, short_address);
    request.cmd_ctrl.dst_ep = endpoint_id;
    request.cmd_ctrl.src_ep = 1;
    request.cmd_ctrl.cluster_id = EZB_ZCL_CLUSTER_ID_LEVEL;
    request.cmd_ctrl.manuf_code = EZB_ZCL_STD_MANUF_CODE;
    request.cmd_ctrl.fc.manuf_specific = EZB_ZCL_NOT_MANUFACTURER_SPECIFIC;
    request.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV;
    request.cmd_ctrl.fc.dis_default_rsp =
        EZB_ZCL_FRAME_ENABLE_DEFAULT_RESPONSE;
    request.payload.attr_number = 1;
    request.payload.attr_field = &attribute_id;
    const ezb_err_t result = ezb_zcl_read_attr_cmd_req(&request);
    esp_zigbee_lock_release();
    if (result != EZB_ERR_NONE) {
        fail_zigbee_level_read(endpoint->level_state);
        level_read_deadlines_[device_index][endpoint_index] = 0;
        ESP_LOGW(kTag,
                 "Level read request failed: short=0x%04x ep=%u raw=%d",
                 static_cast<unsigned>(short_address),
                 static_cast<unsigned>(endpoint_id), static_cast<int>(result));
        publish();
        return;
    }
    ESP_LOGI(kTag, "Level read request queued: short=0x%04x ep=%u",
             static_cast<unsigned>(short_address),
             static_cast<unsigned>(endpoint_id));
#endif
}

void MultipanService::handle_command(const Command& command) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)command;
#else
    if (command.type == CommandType::InterviewDevice) {
        if (current_.state != MultipanState::running ||
            !current_.zigbee_network_formed) {
            current_.last_error = ESP_ERR_INVALID_STATE;
            publish();
            return;
        }
        const auto device = std::find_if(
            current_device_capabilities_.begin(),
            current_device_capabilities_.begin() +
                current_device_capability_count_,
            [&command](const ZigbeeDeviceCapabilities& item) {
                return item.short_address == command.short_address;
            });
        if (device != current_device_capabilities_.begin() +
                  current_device_capability_count_ &&
            device->interview_pending) {
            current_.last_error = ESP_ERR_INVALID_STATE;
            publish();
            return;
        }
        const esp_err_t remember_status =
            remember_known_short_address(command.short_address);
        if (remember_status == ESP_ERR_INVALID_ARG ||
            remember_status == ESP_ERR_NO_MEM) {
            current_.last_error = remember_status;
            publish();
            return;
        }
        start_device_interview(command.short_address);
        return;
    }

    if (command.type == CommandType::ReadOnOffState) {
        read_device_on_off_state(command.short_address, command.endpoint_id);
        return;
    }

    if (command.type == CommandType::ReadLevelState) {
        read_device_level_state(command.short_address, command.endpoint_id);
        return;
    }

    if (command.type == CommandType::DeviceCommand) {
        if (current_.state != MultipanState::running ||
            !current_.zigbee_network_formed) {
            current_.last_error = ESP_ERR_INVALID_STATE;
            publish();
            return;
        }
        auto device = std::find_if(
            current_device_capabilities_.begin(),
            current_device_capabilities_.begin() +
                current_device_capability_count_,
            [&command](const ZigbeeDeviceCapabilities& item) {
                return item.short_address == command.short_address;
            });
        if (device == current_device_capabilities_.begin() +
                  current_device_capability_count_ ||
            !device->interview_complete || device->interview_failed) {
            current_.last_error = ESP_ERR_INVALID_STATE;
            publish();
            return;
        }
        const auto endpoint = std::find_if(
            device->endpoints.begin(),
            device->endpoints.begin() + device->endpoint_count,
            [&command](const ZigbeeEndpointCapabilities& item) {
                return item.endpoint_id == command.endpoint_id;
            });
        if (endpoint == device->endpoints.begin() + device->endpoint_count) {
            current_.last_error = ESP_ERR_NOT_FOUND;
            publish();
            return;
        }
        const std::uint16_t cluster_id =
            command.device_command_kind == ZigbeeDeviceCommandKind::set_on_off
                ? EZB_ZCL_CLUSTER_ID_ON_OFF
                : EZB_ZCL_CLUSTER_ID_LEVEL;
        const auto cluster_end = endpoint->input_clusters.begin() +
            endpoint->input_cluster_count;
        if (std::find(endpoint->input_clusters.begin(), cluster_end,
                      cluster_id) == cluster_end) {
            current_.last_error = ESP_ERR_NOT_SUPPORTED;
            publish();
            return;
        }
        const ZigbeeDeviceCommand audit_command{
            command.short_address, command.endpoint_id,
            command.device_command_kind, command.value};
        esp_err_t status = persist_device_command_audit(audit_command);
        if (status != ESP_OK) {
            current_.last_error = status;
            publish();
            return;
        }
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
            current_.last_error = ESP_ERR_TIMEOUT;
            publish();
            return;
        }
        ezb_zcl_cluster_cmd_ctrl_t control{};
        ezb_address_set_short(&control.dst_addr, command.short_address);
        control.dst_ep = command.endpoint_id;
        control.src_ep = 1;
        control.dis_default_rsp = false;
        ezb_err_t result = EZB_ERR_FAIL;
        if (command.device_command_kind ==
            ZigbeeDeviceCommandKind::set_on_off) {
            const ezb_zcl_on_off_cmd_t request{control};
            result = command.value != 0
                ? ezb_zcl_on_off_on_cmd_req(&request)
                : ezb_zcl_on_off_off_cmd_req(&request);
        } else {
            ezb_zcl_level_move_to_level_cmd_t request{};
            request.cmd_ctrl = control;
            request.payload.level = command.value;
            request.payload.transition_time = 0;
            const auto execute_if_off = static_cast<std::uint8_t>(
                1U << EZB_ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF);
            request.payload.options_mask = execute_if_off;
            request.payload.options_override = execute_if_off;
            result = ezb_zcl_level_move_to_level_cmd_req(&request);
        }
        esp_zigbee_lock_release();
        current_.last_error = esp_zigbee_err_to_esp(result);
        ESP_LOGI(kTag, "Device command queued: short=0x%04x ep=%u kind=%u value=%u result=%d",
                 static_cast<unsigned>(command.short_address),
                 static_cast<unsigned>(command.endpoint_id),
                 static_cast<unsigned>(command.device_command_kind),
             static_cast<unsigned>(command.value),
                 static_cast<int>(result));
        publish();
        if (result == EZB_ERR_NONE) {
            if (command.device_command_kind ==
                ZigbeeDeviceCommandKind::set_on_off) {
                read_device_on_off_state(command.short_address,
                                         command.endpoint_id);
            } else {
                read_device_level_state(command.short_address,
                                        command.endpoint_id);
            }
        }
        return;
    }

    if (command.type == CommandType::StartNetworks) {
        start_networks();
        return;
    }

    if (command.type == CommandType::SearchNearby) {
        const auto action = select_zigbee_search_action(
            current_.zigbee_started && current_.zigbee_bdb_ready,
            current_.zigbee_network_formed);
        if (action == ZigbeeSearchAction::unavailable) {
            current_.last_error = ESP_ERR_INVALID_STATE;
            publish();
            return;
        }
        if (action == ZigbeeSearchAction::open_join_window) {
            handle_command({CommandType::OpenJoinWindow, kMaxJoinWindowSeconds});
            return;
        }
        open_join_after_formation_ = true;
        start_networks();
        if (!current_.network_start_requested) {
            open_join_after_formation_ = false;
        }
        return;
    }

    if (!current_.zigbee_network_formed) {
        current_.last_error = ESP_ERR_INVALID_STATE;
        publish();
        return;
    }

    esp_err_t status = persist_pairing_audit(command.duration_seconds);
    if (status != ESP_OK) {
        current_.last_error = status;
        publish();
        return;
    }
    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        current_.last_error = ESP_ERR_TIMEOUT;
        publish();
        return;
    }
    const ezb_err_t zigbee_status = ezb_bdb_open_network(command.duration_seconds);
    esp_zigbee_lock_release();
    status = esp_zigbee_err_to_esp(zigbee_status);
    current_.last_error = status;
    if (status == ESP_OK) {
        pairing_deadline_ = xTaskGetTickCount() +
            pdMS_TO_TICKS(static_cast<std::uint32_t>(command.duration_seconds) * 1000U);
    }
    publish();
#endif
}

void MultipanService::start_networks() noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    return;
#else
    if (current_.state != MultipanState::running ||
        !current_.zigbee_started || !current_.zigbee_bdb_ready ||
        current_.network_start_requested ||
        current_.zigbee_formation_timed_out) {
        current_.last_error = ESP_ERR_INVALID_STATE;
        publish();
        return;
    }

    esp_err_t status = persist_network_start_audit();
    if (status != ESP_OK) {
        current_.last_error = status;
        publish();
        return;
    }

    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
        current_.last_error = ESP_ERR_TIMEOUT;
        publish();
        return;
    }
    if (!ezb_bdb_is_factory_new()) {
        esp_zigbee_lock_release();
        current_.last_error = ESP_ERR_INVALID_STATE;
        publish();
        return;
    }
    const ezb_err_t result = ezb_bdb_start_top_level_commissioning(
        EZB_BDB_MODE_NETWORK_FORMATION);
    esp_zigbee_lock_release();
    const esp_err_t zigbee_status = esp_zigbee_err_to_esp(result);
    current_.last_error = zigbee_status;
    current_.network_start_requested = zigbee_status == ESP_OK;
    current_.zigbee_formation_timed_out = false;
    current_.last_bdb_result = static_cast<std::int32_t>(result);
    current_.last_bdb_stage = kBdbStageFormationStart;
    if (zigbee_status == ESP_OK) {
        current_.last_bdb_status = EZB_BDB_STATUS_IN_PROGRESS;
        formation_deadline_ = xTaskGetTickCount() +
            pdMS_TO_TICKS(kFormationTimeoutMs);
    } else {
        formation_deadline_ = 0;
    }
    publish();
#endif
}

esp_err_t MultipanService::persist_network_start_audit() noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(kStoragePartition, kAuditNamespace,
                                                NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    std::uint32_t count = 0;
    const esp_err_t read_status = nvs_get_u32(handle, "network_starts", &count);
    if (read_status != ESP_OK && read_status != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return read_status;
    }
    status = nvs_set_u32(handle, "network_starts", count + 1U);
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, kNetworkStartUptimeKey,
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

esp_err_t MultipanService::persist_pairing_audit(
    std::uint8_t duration_seconds) noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(kStoragePartition, kAuditNamespace,
                                                NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    std::uint32_t count = 0;
    const esp_err_t read_status = nvs_get_u32(handle, "pair_count", &count);
    if (read_status != ESP_OK && read_status != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return read_status;
    }
    status = nvs_set_u32(handle, "pair_count", count + 1U);
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, "pair_window_s", duration_seconds);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, "pair_uptime_s",
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

esp_err_t MultipanService::persist_device_command_audit(
    const ZigbeeDeviceCommand& command) noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open_from_partition(kStoragePartition, kAuditNamespace,
                                                NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    std::uint32_t count = 0;
    const esp_err_t read_status = nvs_get_u32(handle, "dev_cmds", &count);
    if (read_status != ESP_OK && read_status != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return read_status;
    }
    status = nvs_set_u32(handle, "dev_cmds", count + 1U);
    if (status == ESP_OK) {
        status = nvs_set_u16(handle, "dev_addr", command.short_address);
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, "dev_ep", command.endpoint_id);
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, "dev_kind",
            static_cast<std::uint8_t>(command.kind));
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, "dev_value", command.value);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, "dev_time_s",
            static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

void MultipanService::refresh_thread_status() noexcept {
#if CONFIG_HUB_MULTIPAN_ENABLE
    if (!esp_openthread_lock_acquire(0)) {
        return;
    }
    otInstance* instance = esp_openthread_get_instance();
    if (instance != nullptr) {
        const bool commissioned = otDatasetIsCommissioned(instance);
        const otDeviceRole role = otThreadGetDeviceRole(instance);
        current_.thread_role = map_thread_role(role, commissioned);
        if (commissioned && !current_.thread_border_router_ready) {
            const esp_err_t status = esp_openthread_border_router_init();
            if (status == ESP_OK) {
                current_.thread_border_router_ready = true;
                const otError start_status = otIp6SetEnabled(instance, true);
                if (start_status == OT_ERROR_NONE) {
                    (void)otThreadSetEnabled(instance, true);
                }
            } else if (status != ESP_ERR_INVALID_STATE) {
                current_.last_error = status;
            }
        }
    }
    esp_openthread_lock_release();
#endif
}

void MultipanService::publish() noexcept {
    if (snapshot_mutex_ == nullptr) {
        return;
    }
    ++current_.revision;
    if (xSemaphoreTake(snapshot_mutex_, portMAX_DELAY) == pdTRUE) {
        published_ = current_;
        published_device_capabilities_ = current_device_capabilities_;
        published_device_capability_count_ = current_device_capability_count_;
        xSemaphoreGive(snapshot_mutex_);
    }
}

bool MultipanService::enqueue_stack_event(const StackEvent& event) noexcept {
    return stack_event_queue_ != nullptr &&
           xQueueSend(stack_event_queue_, &event, 0) == pdTRUE;
}

void MultipanService::fail(esp_err_t error) noexcept {
    current_.state = MultipanState::failed;
    current_.last_error = error;
    ESP_LOGE(kTag, "Multipan initialization failed: %s", esp_err_to_name(error));
    publish();
}

}