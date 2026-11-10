#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "sdkconfig.h"
#include "zigbee_coordinator.hpp"
#include "zigbee_known_devices.hpp"
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
#include "esp_zigbee.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_provisioning.hpp"

namespace hub::radio {

static_assert(kMaxPersistedZigbeeShortAddresses == kMaxZigbeeShortAddresses);

enum class MultipanState : std::uint8_t {
    disabled,
    needs_uart_configuration,
    starting,
    running,
    failed,
};

enum class ThreadRole : std::uint8_t {
    unknown,
    uncommissioned,
    disabled,
    detached,
    child,
    router,
    leader,
};

struct MultipanSnapshot {
    std::uint32_t revision = 0;
    MultipanState state = MultipanState::disabled;
    ThreadRole thread_role = ThreadRole::unknown;
    bool zigbee_started = false;
    bool zigbee_bdb_ready = false;
    bool zigbee_network_formed = false;
    bool network_start_requested = false;
    bool zigbee_formation_timed_out = false;
    bool thread_border_router_ready = false;
    std::uint8_t zigbee_device_count = 0;
    std::array<std::uint16_t, kMaxZigbeeShortAddresses>
        zigbee_short_addresses{};
    std::uint8_t zigbee_short_address_count = 0;
    bool zigbee_short_addresses_truncated = false;
    bool zigbee_device_capabilities_truncated = false;
    std::uint8_t pairing_seconds_remaining = 0;
    std::uint32_t dropped_announcement_events = 0;
    esp_err_t last_error = ESP_OK;
    std::uint16_t last_signal_type = 0;
    std::uint8_t last_bdb_status = 0;
    std::int32_t last_bdb_result = 0;
    std::uint8_t last_bdb_stage = 0;
};

class MultipanService final : public IZigbeeCoordinator {
public:
    explicit MultipanService(wifi::Provisioning& provisioning) noexcept;
    MultipanService(const MultipanService&) = delete;
    MultipanService& operator=(const MultipanService&) = delete;

    // Starts one owner task; returns before radio initialization completes.
    // Repeated calls return ESP_ERR_INVALID_STATE.
    esp_err_t start() noexcept override;
    // Enqueues network formation. UI requires a second confirmation tap; the
    // owner task commits an audit record before changing Zigbee network state.
    esp_err_t request_network_start() noexcept override;
    // Enqueues a 1..180 second join window; audit is committed before opening.
    esp_err_t request_pairing_window(
        std::uint8_t duration_seconds) noexcept override;
    esp_err_t request_search() noexcept override;
    esp_err_t request_device_interview(
        std::uint16_t short_address) noexcept override;
    esp_err_t request_device_on_off_read(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept override;
    esp_err_t request_device_level_read(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept override;
    bool read_device_capabilities(
        std::uint16_t short_address,
        ZigbeeDeviceCapabilities* capabilities) const noexcept override;
    esp_err_t request_device_command(
        const ZigbeeDeviceCommand& command) noexcept override;
    // Copies the latest owner-task snapshot under a mutex; exposes no device IDs.
    bool read_snapshot(MultipanSnapshot* snapshot) const noexcept;
    bool read_snapshot(
        ZigbeeCoordinatorSnapshot* snapshot) const noexcept override;

private:
    enum class EventType : std::uint8_t {
        ZigbeeStarted,
        BdbStartup,
        BdbDeviceStart,
        NetworkFormed,
        DeviceAnnounced,
        ActiveEndpoints,
        SimpleDescriptor,
        OnOffAttribute,
        LevelAttribute,
        DefaultResponse,
        PairingWindowChanged,
        Failure,
    };

    struct StackEvent {
        EventType type = EventType::NetworkFormed;
        std::uint16_t short_address = 0;
        std::uint8_t value = 0;
        esp_err_t error = ESP_OK;
        std::uint16_t signal_type = 0;
        bool has_bdb_status = false;
        bool factory_new = false;
        bool network_joined = false;
        std::uint8_t endpoint_count = 0;
        std::uint8_t endpoint_id = 0;
        std::uint8_t input_cluster_count = 0;
        std::uint8_t output_cluster_count = 0;
        std::uint16_t cluster_id = 0;
        std::uint16_t attribute_id = 0;
        std::uint8_t attribute_type = 0;
        std::uint8_t attribute_status = 0;
        std::uint16_t profile_id = 0;
        std::uint16_t device_id = 0;
        bool truncated = false;
        std::array<std::uint8_t, kMaxZigbeeEndpointsPerDevice>
            endpoint_ids{};
        std::array<std::uint16_t, kMaxZigbeeClustersPerEndpoint>
            input_clusters{};
        std::array<std::uint16_t, kMaxZigbeeClustersPerEndpoint>
            output_clusters{};
    };

    struct InterviewContext {
        MultipanService* service = nullptr;
        std::uint16_t short_address = 0;
        std::uint8_t endpoint_id = 0;
    };

    enum class CommandType : std::uint8_t {
        StartNetworks,
        OpenJoinWindow,
        SearchNearby,
        InterviewDevice,
        ReadOnOffState,
        ReadLevelState,
        DeviceCommand,
    };

    struct Command {
        CommandType type = CommandType::StartNetworks;
        std::uint8_t duration_seconds = 0;
        std::uint16_t short_address = 0;
        std::uint8_t endpoint_id = 0;
        ZigbeeDeviceCommandKind device_command_kind =
            ZigbeeDeviceCommandKind::set_on_off;
        std::uint8_t value = 0;
    };

    static void owner_task(void* context);
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    static void zigbee_task(void* context);
    static bool zigbee_signal_handler(const ezb_app_signal_t* signal);
    static void active_endpoints_callback(
        const ezb_zdo_active_ep_req_result_t* result, void* context);
    static void simple_descriptor_callback(
        const ezb_zdo_simple_desc_req_result_t* result, void* context);
    static void zcl_core_action_handler(
        ezb_zcl_core_action_callback_id_t callback_id, void* message);
#endif
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    static void spinel_reset_failure_handler();
#endif

    void run() noexcept;
    void handle_stack_event(const StackEvent& event) noexcept;
    void handle_command(const Command& command) noexcept;
    void start_device_interview(std::uint16_t short_address) noexcept;
    void request_simple_descriptor(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept;
    esp_err_t load_known_devices() noexcept;
    esp_err_t persist_known_devices() noexcept;
    esp_err_t remember_known_short_address(
        std::uint16_t short_address) noexcept;
    void start_next_restored_interview() noexcept;
    void finish_restored_interview(std::uint16_t short_address) noexcept;
    void read_device_on_off_state(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept;
    void read_device_level_state(
        std::uint16_t short_address, std::uint8_t endpoint_id) noexcept;
    void start_networks() noexcept;
    esp_err_t persist_network_start_audit() noexcept;
    esp_err_t persist_pairing_audit(std::uint8_t duration_seconds) noexcept;
    esp_err_t persist_device_command_audit(
        const ZigbeeDeviceCommand& command) noexcept;
    void refresh_thread_status() noexcept;
    void fail(esp_err_t error) noexcept;
    void publish() noexcept;
    bool enqueue_stack_event(const StackEvent& event) noexcept;

    wifi::Provisioning& provisioning_;
    QueueHandle_t stack_event_queue_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    SemaphoreHandle_t snapshot_mutex_ = nullptr;
    TaskHandle_t owner_task_ = nullptr;
    TaskHandle_t zigbee_task_ = nullptr;
    MultipanSnapshot current_{};
    MultipanSnapshot published_{};
    std::array<ZigbeeDeviceCapabilities, kMaxZigbeeShortAddresses>
        current_device_capabilities_{};
    std::array<ZigbeeDeviceCapabilities, kMaxZigbeeShortAddresses>
        published_device_capabilities_{};
    std::uint8_t current_device_capability_count_ = 0;
    std::uint8_t published_device_capability_count_ = 0;
    ZigbeeKnownDevices known_devices_{};
    std::uint8_t restored_interview_cursor_ = 0;
    std::uint16_t restored_interview_address_ = 0;
    bool restoring_known_devices_ = false;
    std::array<InterviewContext, kMaxZigbeeShortAddresses>
        interview_contexts_{};
    std::array<std::uint8_t, kMaxZigbeeShortAddresses>
        interview_next_endpoint_{};
    std::array<TickType_t, kMaxZigbeeShortAddresses>
        interview_deadlines_{};
    std::array<std::array<TickType_t, kMaxZigbeeEndpointsPerDevice>,
               kMaxZigbeeShortAddresses> on_off_read_deadlines_{};
    std::array<std::array<TickType_t, kMaxZigbeeEndpointsPerDevice>,
               kMaxZigbeeShortAddresses> level_read_deadlines_{};
    TickType_t pairing_deadline_ = 0;
    TickType_t formation_deadline_ = 0;
    bool open_join_after_formation_ = false;
    bool started_ = false;

    static MultipanService* active_instance_;
};

}