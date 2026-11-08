#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "sdkconfig.h"
#include "zigbee_coordinator.hpp"
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
#include "esp_zigbee.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_provisioning.hpp"

namespace hub::radio {

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
    bool zigbee_network_formed = false;
    bool network_start_requested = false;
    bool thread_border_router_ready = false;
    std::uint8_t zigbee_device_count = 0;
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
    // Copies the latest owner-task snapshot under a mutex; exposes no device IDs.
    bool read_snapshot(MultipanSnapshot* snapshot) const noexcept;
    bool read_snapshot(
        ZigbeeCoordinatorSnapshot* snapshot) const noexcept override;

private:
    enum class EventType : std::uint8_t {
        ZigbeeStarted,
        NetworkFormed,
        DeviceAnnounced,
        PairingWindowChanged,
        Failure,
    };

    struct StackEvent {
        EventType type = EventType::NetworkFormed;
        std::uint16_t short_address = 0;
        std::uint64_t ieee_address = 0;
        std::uint8_t value = 0;
        esp_err_t error = ESP_OK;
    };

    enum class CommandType : std::uint8_t {
        StartNetworks,
        OpenJoinWindow,
        SearchNearby,
    };

    struct Command {
        CommandType type = CommandType::StartNetworks;
        std::uint8_t duration_seconds = 0;
    };

    static void owner_task(void* context);
#if CONFIG_HUB_MULTIPAN_ENABLE || CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    static void zigbee_task(void* context);
    static bool zigbee_signal_handler(const ezb_app_signal_t* signal);
#endif
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    static void spinel_reset_failure_handler();
#endif

    void run() noexcept;
    void handle_stack_event(const StackEvent& event) noexcept;
    void handle_command(const Command& command) noexcept;
    void start_networks() noexcept;
    esp_err_t persist_network_start_audit() noexcept;
    esp_err_t persist_pairing_audit(std::uint8_t duration_seconds) noexcept;
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
    std::array<std::uint64_t, 64> announced_devices_{};
    std::size_t announced_device_count_ = 0;
    TickType_t pairing_deadline_ = 0;
    bool open_join_after_formation_ = false;
    bool started_ = false;

    static MultipanService* active_instance_;
};

}