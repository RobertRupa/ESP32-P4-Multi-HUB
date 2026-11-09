#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "lvgl.h"
#include "zigbee_coordinator.hpp"
#include "wifi_provisioning.hpp"
#include "web_zigbee_approval.hpp"

namespace hub::ui {

class WifiWizard final {
public:
    WifiWizard(wifi::Provisioning& provisioning,
               radio::IZigbeeCoordinator& zigbee,
               diagnostics::WebZigbeeApproval& web_approval) noexcept;
    esp_err_t start(lv_display_t* display) noexcept;

private:
    enum class Page : std::uint8_t {
        Home,
        Diagnostics,
        Devices,
        Networks,
        Password,
        Status,
    };

    struct AccessPointButton {
        WifiWizard* wizard = nullptr;
        std::uint8_t index = 0;
    };

    static void poll_timer(lv_timer_t* timer);
    static void language_event(lv_event_t* event);
    static void scan_event(lv_event_t* event);
    static void access_point_event(lv_event_t* event);
    static void show_password_event(lv_event_t* event);
    static void connect_event(lv_event_t* event);
    static void back_event(lv_event_t* event);
    static void home_event(lv_event_t* event);
    static void diagnostics_event(lv_event_t* event);
    static void diagnostics_refresh_event(lv_event_t* event);
    static void devices_event(lv_event_t* event);
    static void network_start_event(lv_event_t* event);
    static void pairing_window_event(lv_event_t* event);
    static void scan_nearby_event(lv_event_t* event);

    const char* text(const char* english, const char* polish) const noexcept;
    void render();
    void render_home();
    void render_diagnostics();
    void render_devices();
    void render_networks();
    void render_password();
    void render_status();
    void select_network(std::uint8_t index);
    void connect_selected();
    void request_scan();
    lv_obj_t* add_button(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t width, lv_coord_t height, const char* label,
                         lv_event_cb_t callback, void* user_data = nullptr);
    lv_obj_t* add_label(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t width, const char* value, std::uint32_t color,
                        std::uint16_t size);

    wifi::Provisioning& provisioning_;
    radio::IZigbeeCoordinator& zigbee_;
    diagnostics::WebZigbeeApproval& web_approval_;
    lv_display_t* display_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* password_field_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_timer_t* poll_timer_ = nullptr;
    wifi::ProvisioningUpdate update_{};
    radio::ZigbeeCoordinatorSnapshot zigbee_update_{};
    diagnostics::WebZigbeeRequestSnapshot web_request_{};
    wifi::AccessPoint selected_network_{};
    std::array<AccessPointButton, wifi::kMaxAccessPoints> access_point_buttons_{};
    Page page_ = Page::Home;
    bool polish_ = false;
    bool network_start_confirmation_pending_ = false;
    bool pairing_confirmation_pending_ = false;
    bool scan_nearby_confirmation_pending_ = false;
    bool zigbee_request_pending_ = false;
    bool zigbee_network_creation_pending_ = false;
    esp_err_t zigbee_request_error_ = ESP_OK;
    std::int64_t zigbee_request_started_us_ = 0;
};

}