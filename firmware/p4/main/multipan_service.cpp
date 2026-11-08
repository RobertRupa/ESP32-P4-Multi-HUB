#include "multipan_service.hpp"

#include <algorithm>
#include <cstdio>

#include "esp_log.h"
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
#include "driver/uart.h"
#include "esp_vfs_eventfd.h"
#include "ezbee/zha.h"
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
constexpr char kStoragePartition[] = "nvs";
#else
constexpr char kStoragePartition[] = "wifi_cfg";
#endif
constexpr char kAuditNamespace[] = "radio_audit";
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
constexpr char kManufacturer[] = "\x09" "ESPRESSIF";
constexpr char kModel[] = "\x0B" "P4-ZIGBEE-HOST";
#endif
constexpr std::uint8_t kMaxJoinWindowSeconds = 180;
constexpr std::size_t kMaxTrackedDevices = 64;
constexpr std::uint32_t kEventQueueDepth = 32;
constexpr std::uint32_t kCommandQueueDepth = 2;
constexpr std::uint32_t kProvisioningWaitMs = 30000;

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
    snapshot->stack_ready = radio_snapshot.zigbee_started;
    snapshot->network_formed = radio_snapshot.zigbee_network_formed;
    snapshot->search_pending = radio_snapshot.network_start_requested &&
                               !radio_snapshot.zigbee_network_formed;
    snapshot->formation_timed_out = false;
    snapshot->device_count = radio_snapshot.zigbee_device_count;
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
        service->enqueue_stack_event(
            {EventType::Failure, 0, 0, 0, ESP_FAIL});
        vTaskDelete(nullptr);
    }

    const ezb_err_t primary_status =
        ezb_bdb_set_primary_channel_set(1U << 13);
    const ezb_err_t secondary_status =
        ezb_bdb_set_secondary_channel_set(0x07FFF800U);
    if (primary_status != EZB_ERR_NONE || secondary_status != EZB_ERR_NONE ||
        ezb_app_signal_add_handler(zigbee_signal_handler) != ESP_OK) {
        service->enqueue_stack_event(
            {EventType::Failure, 0, 0, 0, ESP_FAIL});
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
        service->enqueue_stack_event(
            {EventType::Failure, 0, 0, 0, ESP_FAIL});
        vTaskDelete(nullptr);
    }

    status = esp_zigbee_start(false);
    if (status != ESP_OK) {
        service->enqueue_stack_event(
            {EventType::Failure, 0, 0, 0, status});
        vTaskDelete(nullptr);
    }
    service->enqueue_stack_event(
        {EventType::ZigbeeStarted, 0, 0, 0, ESP_OK});

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
    (void)service->enqueue_stack_event(
        {EventType::Failure, 0, 0, 0, ESP_ERR_TIMEOUT});
}
#endif

bool MultipanService::zigbee_signal_handler(const ezb_app_signal_t* signal) {
    if (active_instance_ == nullptr || signal == nullptr) {
        return false;
    }

    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    if (type == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const auto* params = static_cast<const ezb_zdo_signal_device_annce_params_t*>(
            ezb_app_signal_get_params(signal));
        if (params == nullptr) {
            return false;
        }
        return active_instance_->enqueue_stack_event({
            EventType::DeviceAnnounced,
            static_cast<std::uint16_t>(params->short_addr),
            params->device_addr.u64,
            0,
            ESP_OK,
        });
    }

    if (type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ||
        type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
        const auto* status = static_cast<const ezb_bdb_comm_status_t*>(
            ezb_app_signal_get_params(signal));
        if (status != nullptr && *status == EZB_BDB_STATUS_SUCCESS) {
            if (!ezb_bdb_is_factory_new()) {
                (void)ezb_bdb_close_network();
                (void)active_instance_->enqueue_stack_event(
                    {EventType::NetworkFormed, 0, 0, 0, ESP_OK});
            }
        }
        return true;
    }

    if (type == EZB_BDB_SIGNAL_FORMATION) {
        const auto* status = static_cast<const ezb_bdb_comm_status_t*>(
            ezb_app_signal_get_params(signal));
        const bool formed = status != nullptr &&
                            *status == EZB_BDB_STATUS_SUCCESS;
        if (formed) {
            (void)ezb_bdb_close_network();
        }
        (void)active_instance_->enqueue_stack_event({
            EventType::NetworkFormed, 0, 0, 0,
            formed ? ESP_OK : ESP_FAIL,
        });
        return true;
    }

    if (type == EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS) {
        const auto* duration = static_cast<const std::uint8_t*>(
            ezb_app_signal_get_params(signal));
        if (duration != nullptr) {
            (void)active_instance_->enqueue_stack_event({
                EventType::PairingWindowChanged, 0, 0, *duration, ESP_OK});
        }
        return true;
    }

    return true;
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
    }
#endif
}

void MultipanService::handle_stack_event(const StackEvent& event) noexcept {
    bool open_join_window = false;
    switch (event.type) {
        case EventType::ZigbeeStarted:
            current_.zigbee_started = event.error == ESP_OK;
            break;
        case EventType::NetworkFormed:
            current_.zigbee_network_formed = event.error == ESP_OK;
            current_.network_start_requested = event.error == ESP_OK;
            if (event.error != ESP_OK) {
                current_.last_error = event.error;
                open_join_after_formation_ = false;
            } else if (open_join_after_formation_) {
                open_join_after_formation_ = false;
                open_join_window = true;
            }
            break;
        case EventType::DeviceAnnounced: {
            const auto begin = announced_devices_.begin();
            const auto end = begin + announced_device_count_;
            const auto found = std::find(begin, end, event.ieee_address);
            if (found == end && announced_device_count_ < announced_devices_.size()) {
                announced_devices_[announced_device_count_++] = event.ieee_address;
                current_.zigbee_device_count = static_cast<std::uint8_t>(
                    announced_device_count_);
            } else if (found == end) {
                ++current_.dropped_announcement_events;
            }
            break;
        }
        case EventType::PairingWindowChanged:
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
            break;
    }
    publish();
    if (open_join_window) {
        handle_command({CommandType::OpenJoinWindow, kMaxJoinWindowSeconds});
    }
}

void MultipanService::handle_command(const Command& command) noexcept {
#if !CONFIG_HUB_MULTIPAN_ENABLE && !CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    (void)command;
#else
    if (command.type == CommandType::StartNetworks) {
        start_networks();
        return;
    }

    if (command.type == CommandType::SearchNearby) {
        const auto action = select_zigbee_search_action(
            current_.zigbee_started, current_.zigbee_network_formed);
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
        !current_.zigbee_started || current_.network_start_requested) {
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
    const esp_err_t zigbee_status = esp_zigbee_err_to_esp(
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION));
    esp_zigbee_lock_release();
    current_.last_error = zigbee_status;
    current_.network_start_requested = zigbee_status == ESP_OK;
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
        status = nvs_set_u32(handle, "network_start_uptime_s",
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