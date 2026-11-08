#include "wifi_wizard.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "system_diagnostics.hpp"

namespace {

constexpr std::uint32_t kBackground = 0x101A20;
constexpr std::uint32_t kSurface = 0x1B2A31;
constexpr std::uint32_t kAccent = 0x39D6A0;
constexpr std::uint32_t kText = 0xF2F6F4;
constexpr std::uint32_t kMuted = 0xA7B8B8;
constexpr std::uint32_t kError = 0xFF8A78;
constexpr std::uint32_t kPollPeriodMs = 180;
constexpr std::size_t kMaxPasswordLength = 63;

}

namespace hub::ui {

WifiWizard::WifiWizard(wifi::Provisioning& provisioning,
                       radio::IZigbeeCoordinator& zigbee,
                       diagnostics::WebZigbeeApproval& web_approval) noexcept
    : provisioning_(provisioning), zigbee_(zigbee), web_approval_(web_approval) {}

esp_err_t WifiWizard::start(lv_display_t* display) noexcept {
    if (display == nullptr || poll_timer_ != nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lvgl_port_lock(0)) {
        return ESP_FAIL;
    }
    display_ = display;
    root_ = lv_display_get_screen_active(display_);
    if (root_ == nullptr) {
        lvgl_port_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    provisioning_.read_snapshot(&update_);

    lv_obj_set_scroll_dir(root_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    poll_timer_ = lv_timer_create(poll_timer, kPollPeriodMs, this);
    if (poll_timer_ == nullptr) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    render();
    lvgl_port_unlock();
    return ESP_OK;
}

const char* WifiWizard::text(const char* english, const char* polish) const noexcept {
    return polish_ ? polish : english;
}

lv_obj_t* WifiWizard::add_label(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t width, const char* value,
                                std::uint32_t color, std::uint16_t size) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, size >= 24 ? &lv_font_montserrat_24 : &lv_font_montserrat_16, 0);
    return label;
}

lv_obj_t* WifiWizard::add_button(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                 lv_coord_t width, lv_coord_t height,
                                 const char* label, lv_event_cb_t callback,
                                 void* user_data) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 12, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t* text_label = lv_label_create(button);
    lv_label_set_text(text_label, label);
    lv_obj_set_width(text_label, width - 24);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(text_label, lv_color_hex(kText), 0);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_16, 0);
    lv_obj_center(text_label);
    return button;
}

void WifiWizard::render() {
    lv_obj_clean(root_);
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);

    const char* title = text("P4 Multi Hub", "P4 Multi Hub");
    const bool password_page = page_ == Page::Password;
    if (password_page) {
        title = text("Join network", "Dolacz do sieci");
    } else if (page_ == Page::Networks) {
        title = text("Wi-Fi setup", "Konfiguracja Wi-Fi");
    } else if (page_ == Page::Diagnostics) {
        title = text("System status", "Stan systemu");
    } else if (page_ == Page::Devices) {
        title = text("Devices and Zigbee", "Urzadzenia i Zigbee");
    } else if (page_ == Page::Status) {
        if (update_.state == wifi::ProvisioningState::Connected) {
            title = text("Connected", "Polaczono");
        } else if (update_.state == wifi::ProvisioningState::Failed) {
            title = text("Connection issue", "Problem z polaczeniem");
        } else {
            title = text("Connecting", "Laczenie");
        }
    }

    const lv_coord_t language_x = screen_width - 106;
    const bool show_navigation = page_ != Page::Home;
    const lv_coord_t title_width = show_navigation ? screen_width - 244 : screen_width - 140;
    add_label(root_, 24, 26, title_width, title, kText, 24);
    if (show_navigation) {
        add_button(root_, screen_width - 196, 18, 84, 48,
                   password_page ? text("Back", "Wroc") : text("Home", "Start"),
                   password_page ? back_event : home_event, this);
    }
    lv_obj_t* language = add_button(root_, language_x, 18, 82, 48,
                                    polish_ ? "PL" : "EN", language_event, this);
    lv_obj_set_style_bg_color(language, lv_color_hex(kAccent), 0);

    if (page_ == Page::Home) {
        render_home();
    } else if (page_ == Page::Password) {
        render_password();
    } else if (page_ == Page::Status) {
        render_status();
    } else if (page_ == Page::Diagnostics) {
        render_diagnostics();
    } else if (page_ == Page::Devices) {
        render_devices();
    } else {
        render_networks();
    }
}

void WifiWizard::render_home() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const lv_coord_t content_width = screen_width - 48;
    const bool compact = screen_height <= 600;
    const char* wifi_status = text("Waiting for Wi-Fi", "Oczekiwanie na Wi-Fi");
    std::uint32_t wifi_color = kMuted;
    if (update_.state == wifi::ProvisioningState::Connected) {
        wifi_status = text("Connected", "Polaczono");
        wifi_color = kAccent;
    } else if (update_.state == wifi::ProvisioningState::Connecting) {
        wifi_status = text("Connecting", "Laczenie");
    } else if (update_.state == wifi::ProvisioningState::Failed) {
        wifi_status = text("Needs attention", "Wymaga uwagi");
        wifi_color = kError;
    }

    const lv_coord_t card_y = compact ? 78 : 104;
    const lv_coord_t card_height = compact ? 176 : 210;
    lv_obj_t* card = lv_obj_create(root_);
    lv_obj_set_size(card, content_width, card_height);
    lv_obj_set_pos(card, 24, card_y);
    lv_obj_set_style_bg_color(card, lv_color_hex(kSurface), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    add_label(card, 0, 0, content_width - 84,
              text("Network", "Siec"), kMuted, 16);
    add_label(card, 0, 26, content_width - 84, wifi_status, wifi_color, 24);
    const char* ip_status = update_.ipv4_address[0] != '\0'
                                ? update_.ipv4_address
                                : text("No address", "Brak adresu");
    add_label(card, 0, 64, content_width - 84, ip_status, kText, 16);

    const char* store_status = update_.credential_store_ready
                                   ? text("Encrypted storage ready", "Szyfrowany zapis gotowy")
                                   : text("Encrypted storage unavailable", "Szyfrowany zapis niedostepny");
    add_label(card, 0, 104, content_width - 84, store_status,
              update_.credential_store_ready ? kAccent : kError, 16);

    const lv_coord_t setup_y = card_y + card_height + (compact ? 14 : 22);
    const lv_coord_t action_gap = 12;
    const lv_coord_t action_width = (content_width - action_gap) / 2;
    add_button(root_, 24, setup_y, action_width, compact ? 48 : 56,
               text("Configure Wi-Fi", "Konfiguruj Wi-Fi"), scan_event, this);
    add_button(root_, 24 + action_width + action_gap, setup_y, action_width,
               compact ? 48 : 56, text("System status", "Stan systemu"),
               diagnostics_event, this);
    const lv_coord_t devices_y = setup_y + (compact ? 56 : 66);
    add_button(root_, 24, devices_y, content_width, compact ? 44 : 50,
               text("Devices", "Urzadzenia"), devices_event, this);

    char diagnostics_url[72]{};
    if (update_.ipv4_address[0] != '\0') {
        std::snprintf(diagnostics_url, sizeof(diagnostics_url),
                      "https://MultiHub.local/");
    } else {
        std::snprintf(diagnostics_url, sizeof(diagnostics_url),
                      "%s", text("HTTPS controls available after connection",
                                  "HTTPS po polaczeniu z siecia"));
    }
    add_label(root_, 24, setup_y + (compact ? 108 : 126), content_width,
              diagnostics_url, kMuted, 16);

    if (update_.credentials_saved) {
        add_label(root_, 24, setup_y + (compact ? 140 : 160), content_width,
                  text("Wi-Fi profile saved securely", "Profil Wi-Fi zapisany bezpiecznie"),
                  kAccent, 16);
    }
}

void WifiWizard::render_devices() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const lv_coord_t content_width = screen_width - 48;
    const bool compact = screen_height <= 600;
    const lv_coord_t row_y = compact ? 78 : 92;
    const lv_coord_t row_gap = compact ? 32 : 40;
    zigbee_.read_snapshot(&zigbee_update_);

    char row[96]{};
    const char* coordinator_state = !zigbee_update_.available
        ? text("Unavailable", "Niedostepny")
        : (zigbee_update_.stack_ready ? text("Ready", "Gotowy")
                                      : text("Starting", "Uruchamianie"));
    std::snprintf(row, sizeof(row), "%s: %s",
                  text("C6 coordinator", "Koordynator C6"), coordinator_state);
    add_label(root_, 24, row_y, content_width, row,
              zigbee_update_.stack_ready ? kAccent : kMuted, 16);
    const char* network_state = zigbee_update_.formation_timed_out
        ? text("Formation timed out", "Przekroczono czas tworzenia")
        : (zigbee_update_.network_formed
            ? text("Formed", "Utworzona")
            : (zigbee_update_.search_pending
                ? text("Formation in progress", "Trwa tworzenie sieci")
                : text("Not formed", "Nieutworzona")));
    std::snprintf(row, sizeof(row), "%s: %s",
                  text("Zigbee network", "Siec Zigbee"), network_state);
    add_label(root_, 24, row_y + row_gap, content_width, row,
              zigbee_update_.network_formed ? kAccent : kMuted, 16);
    std::snprintf(row, sizeof(row), "%s: %u",
                  text("Found devices", "Znalezione urzadzenia"),
                  static_cast<unsigned>(zigbee_update_.device_count));
    add_label(root_, 24, row_y + row_gap * 2, content_width, row, kText, 16);

    if (zigbee_update_.pairing_seconds_remaining > 0) {
        std::snprintf(row, sizeof(row), "%s: %u s",
                      text("Search window", "Okno wyszukiwania"),
                      static_cast<unsigned>(zigbee_update_.pairing_seconds_remaining));
    } else {
        std::snprintf(row, sizeof(row), "%s: %s",
                      text("Search window", "Okno wyszukiwania"),
                      text("Closed", "Zamkniete"));
    }
    add_label(root_, 24, row_y + row_gap * 3, content_width, row, kText, 16);

    const lv_coord_t buttons_y = row_y + row_gap * 4 + 8;
    if (web_request_.state == diagnostics::WebZigbeeRequestState::awaiting_touch) {
        const char* action = web_request_.action == diagnostics::WebZigbeeAction::create_network
            ? text("Website requested network creation", "Strona prosi o utworzenie sieci")
            : (web_request_.action == diagnostics::WebZigbeeAction::scan_nearby
                ? text("Website requested nearby scan", "Strona prosi o skan pobliskich urzadzen")
                : text("Website requested device search", "Strona prosi o wyszukiwanie urzadzen"));
        add_label(root_, 24, buttons_y, content_width, action, kAccent, 16);
        const lv_coord_t button_width = (content_width - 10) / 2;
        add_button(root_, 24, buttons_y + 28, button_width, compact ? 42 : 46,
                   text("Approve", "Zatwierdz"), web_approval_confirm_event, this);
        add_button(root_, 34 + button_width, buttons_y + 28, button_width,
                   compact ? 42 : 46, text("Reject", "Odrzuc"),
                   web_approval_reject_event, this);
    } else if (zigbee_request_pending_ || zigbee_update_.search_pending) {
        add_label(root_, 24, buttons_y + 12, content_width,
                  text("Zigbee request pending", "Zadanie Zigbee oczekuje"), kMuted, 16);
    } else if (!zigbee_update_.available || !zigbee_update_.stack_ready) {
        add_label(root_, 24, buttons_y + 12, content_width,
                  text("Coordinator unavailable", "Koordynator niedostepny"), kError, 16);
    } else if (zigbee_update_.formation_timed_out) {
        add_label(root_, 24, buttons_y + 12, content_width,
                  text("Formation timed out; coordinator restart required",
                       "Przekroczono czas tworzenia; wymagany restart koordynatora"),
                  kError, 16);
    } else if (!zigbee_update_.network_formed) {
        add_button(root_, 24, buttons_y, content_width, compact ? 42 : 46,
                   scan_nearby_confirmation_pending_
                       ? text("Confirm nearby scan", "Potwierdz skan pobliskich")
                       : text("Scan nearby Zigbee devices", "Skanuj pobliskie urzadzenia Zigbee"),
                   scan_nearby_event, this);
        add_button(root_, 24, buttons_y + (compact ? 48 : 54), content_width,
                   compact ? 42 : 46,
                   network_start_confirmation_pending_
                       ? text("Confirm network creation", "Potwierdz utworzenie sieci")
                       : text("Create Zigbee network", "Utworz siec Zigbee"),
                   network_start_event, this);
    } else if (zigbee_update_.pairing_seconds_remaining == 0) {
        add_button(root_, 24, buttons_y, content_width, compact ? 42 : 46,
                   scan_nearby_confirmation_pending_
                       ? text("Confirm nearby scan", "Potwierdz skan pobliskich")
                       : text("Scan nearby Zigbee devices", "Skanuj pobliskie urzadzenia Zigbee"),
                   scan_nearby_event, this);
        add_button(root_, 24, buttons_y + (compact ? 48 : 54), content_width,
                   compact ? 42 : 46,
                   pairing_confirmation_pending_
                       ? text("Confirm 180-second search", "Potwierdz wyszukiwanie 180 s")
                       : text("Open join window (180s)", "Otworz okno dolaczania (180 s)"),
                   pairing_window_event, this);
    } else {
        add_label(root_, 24, buttons_y + 12, content_width,
                  text("Join window is open", "Okno dolaczania jest otwarte"), kAccent, 16);
    }
    if (zigbee_request_error_ != ESP_OK || zigbee_update_.last_error != ESP_OK) {
        const esp_err_t error = zigbee_request_error_ != ESP_OK
            ? zigbee_request_error_ : zigbee_update_.last_error;
        std::snprintf(row, sizeof(row), "%s: %ld",
                      text("Zigbee error", "Blad Zigbee"),
                      static_cast<long>(error));
        add_label(root_, 24, screen_height - 34, content_width, row, kError, 16);
    }
    std::snprintf(row, sizeof(row), "%s: 0x%04x / %u",
                  text("BDB signal/status", "Sygnal/status BDB"),
                  static_cast<unsigned>(zigbee_update_.last_signal_type),
                  static_cast<unsigned>(zigbee_update_.last_bdb_status));
    add_label(root_, 24, screen_height - 64, content_width, row, kMuted, 16);
}

void WifiWizard::render_diagnostics() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const lv_coord_t content_width = screen_width - 48;
    const bool compact = screen_height <= 600;
    const lv_coord_t row_y = compact ? 82 : 94;
    const lv_coord_t row_gap = compact ? 38 : 44;
    const auto system = diagnostics::read_system_snapshot();

    const char* connection = text("Starting", "Uruchamianie");
    if (update_.state == wifi::ProvisioningState::Connected) {
        connection = text("Connected", "Polaczono");
    } else if (update_.state == wifi::ProvisioningState::Connecting) {
        connection = text("Connecting", "Laczenie");
    } else if (update_.state == wifi::ProvisioningState::Scanning) {
        connection = text("Scanning", "Skanowanie");
    } else if (update_.state == wifi::ProvisioningState::Failed) {
        connection = text("Needs attention", "Wymaga uwagi");
    } else if (update_.state == wifi::ProvisioningState::NetworksReady) {
        connection = text("Ready", "Gotowe");
    }

    char row[96]{};
    std::snprintf(row, sizeof(row), "%s: %s", text("Wi-Fi", "Wi-Fi"), connection);
    add_label(root_, 24, row_y, content_width, row,
              update_.state == wifi::ProvisioningState::Connected ? kAccent : kText, 16);

    if (update_.rssi_valid) {
        std::snprintf(row, sizeof(row), "%s: %d dBm", text("Wi-Fi signal", "Sygnal Wi-Fi"),
                      static_cast<int>(update_.rssi_dbm));
    } else {
        std::snprintf(row, sizeof(row), "%s: %s", text("Wi-Fi signal", "Sygnal Wi-Fi"),
                      text("Unavailable", "Niedostepny"));
    }
    add_label(root_, 24, row_y + row_gap, content_width, row, kText, 16);

    std::snprintf(row, sizeof(row), "%s: %s", text("IPv4", "IPv4"),
                  update_.ipv4_address[0] != '\0'
                      ? update_.ipv4_address
                      : text("No address", "Brak adresu"));
    add_label(root_, 24, row_y + row_gap * 2, content_width, row, kText, 16);

    std::snprintf(row, sizeof(row), "%s: %s", text("Encrypted storage", "Szyfrowany zapis"),
                  update_.credential_store_ready ? text("Ready", "Gotowy")
                                                 : text("Unavailable", "Niedostepny"));
    add_label(root_, 24, row_y + row_gap * 3, content_width, row,
              update_.credential_store_ready ? kAccent : kError, 16);

    std::snprintf(row, sizeof(row), "%s: %s", text("Profile saved", "Profil zapisany"),
                  update_.credentials_saved ? text("Yes", "Tak") : text("No", "Nie"));
    add_label(root_, 24, row_y + row_gap * 4, content_width, row, kText, 16);

    std::snprintf(row, sizeof(row), "%s: %s", text("Flash encryption", "Szyfrowanie flash"),
                  system.flash_encryption_enabled ? text("Enabled", "Wlaczone")
                                                  : text("Disabled", "Wylaczone"));
    add_label(root_, 24, row_y + row_gap * 5, content_width, row,
              system.flash_encryption_enabled ? kAccent : kError, 16);

    std::snprintf(row, sizeof(row), "%s: %u KB", text("Free PSRAM", "Wolny PSRAM"),
                  static_cast<unsigned>(system.psram_free_kb));
    add_label(root_, 24, row_y + row_gap * 6, content_width, row, kText, 16);

    std::snprintf(row, sizeof(row), "%s: %u KB", text("Internal heap", "Pamiec wewnetrzna"),
                  static_cast<unsigned>(system.internal_heap_free_kb));
    add_label(root_, 24, row_y + row_gap * 7, content_width, row, kText, 16);

    add_button(root_, 24, row_y + row_gap * 7 + (compact ? 34 : 40),
               content_width, compact ? 48 : 54,
               text("Refresh", "Odswiez"), diagnostics_refresh_event, this);
}

void WifiWizard::render_networks() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const bool compact = screen_height <= 600;
    const lv_coord_t content_width = screen_width - 40;
    const lv_coord_t scan_y = compact ? 94 : 128;
    const lv_coord_t list_y = compact ? 152 : 198;
    const lv_coord_t list_height = std::max<lv_coord_t>(100, screen_height - list_y - 54);
    const lv_coord_t row_height = compact ? 50 : 64;

    add_label(root_, 24, compact ? 66 : 90, content_width,
              text("Choose a 2.4 GHz network", "Wybierz siec 2.4 GHz"), kMuted, 16);

    const char* scan_label = update_.state == wifi::ProvisioningState::Scanning
                                 ? text("Scanning...", "Skanowanie...")
                                 : text("Scan again", "Skanuj ponownie");
    add_button(root_, 20, scan_y, content_width, compact ? 48 : 54, scan_label, scan_event, this);

    lv_obj_t* list = lv_obj_create(root_);
    lv_obj_set_size(list, content_width, list_height);
    lv_obj_set_pos(list, 20, list_y);
    lv_obj_set_style_bg_color(list, lv_color_hex(kSurface), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_pad_all(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

    if (update_.state == wifi::ProvisioningState::Scanning) {
        add_label(list, 12, 18, 390, text("Looking for nearby networks", "Szukanie sieci w poblizu"), kMuted, 16);
    } else if (update_.state == wifi::ProvisioningState::Failed) {
        add_label(list, 12, 18, 390, text("Scan failed. Try again.", "Skanowanie nieudane. Sprobuj ponownie."), kError, 16);
    } else if (update_.network_count == 0) {
        add_label(list, 12, 18, 390, text("No networks found", "Nie znaleziono sieci"), kMuted, 16);
    }

    for (std::uint8_t index = 0; index < update_.network_count; ++index) {
        access_point_buttons_[index] = {this, index};
        const auto& network = update_.networks[index];
        char row[128]{};
        std::snprintf(row, sizeof(row), "%s   %d dBm   %s", network.ssid,
                      static_cast<int>(network.rssi),
                      network.secured ? text("SECURED", "ZABEZP.")
                                      : text("OPEN", "OTWARTA"));
        add_button(list, 0, row_height * index, content_width - 22,
                   row_height - 4, row, access_point_event,
                   &access_point_buttons_[index]);
    }

        add_label(root_, 24, screen_height - 28, content_width,
                text("Credentials are saved securely after connection.",
                    "Dane sa szyfrowane po udanym polaczeniu."), kMuted, 14);
}

void WifiWizard::render_password() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const lv_coord_t content_width = screen_width - 48;
    constexpr lv_coord_t keyboard_y = 214;

    char network_label[80]{};
    std::snprintf(network_label, sizeof(network_label), "%s  -  %s",
                  selected_network_.ssid,
                  selected_network_.secured ? text("secured", "zabezpieczona")
                                            : text("open", "otwarta"));
    add_label(root_, 24, 62, content_width, network_label, kMuted, 16);

    password_field_ = lv_textarea_create(root_);
    lv_obj_set_size(password_field_, content_width, 44);
    lv_obj_set_pos(password_field_, 24, 86);
    lv_textarea_set_one_line(password_field_, true);
    lv_textarea_set_password_mode(password_field_, true);
    lv_textarea_set_max_length(password_field_, kMaxPasswordLength);
    lv_textarea_set_placeholder_text(password_field_, text("Password", "Haslo"));
    lv_obj_set_style_bg_color(password_field_, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(password_field_, lv_color_hex(kText), 0);

    lv_obj_t* show_password = lv_checkbox_create(root_);
    lv_checkbox_set_text(show_password, text("Show password", "Pokaz haslo"));
    lv_obj_set_pos(show_password, 24, 136);
    lv_obj_set_style_text_color(show_password, lv_color_hex(kText), 0);
    lv_obj_add_event_cb(show_password, show_password_event, LV_EVENT_VALUE_CHANGED, this);

    add_button(root_, 24, 166, content_width, 44,
               text("Connect", "Polacz"), connect_event, this);

    lv_obj_t* keyboard = lv_keyboard_create(root_);
    lv_obj_set_size(keyboard, screen_width, screen_height - keyboard_y);
    lv_obj_set_pos(keyboard, 0, keyboard_y);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, password_field_);
}

void WifiWizard::render_status() {
    const lv_coord_t screen_width = lv_display_get_horizontal_resolution(display_);
    const lv_coord_t screen_height = lv_display_get_vertical_resolution(display_);
    const bool compact = screen_height <= 600;
    const lv_coord_t content_width = screen_width - 48;
    if (selected_network_.ssid[0] != '\0') {
        add_label(root_, 24, compact ? 62 : 88, content_width, selected_network_.ssid, kMuted, 16);
    }
    const char* message = "";
    std::uint32_t color = kMuted;
    if (update_.state == wifi::ProvisioningState::Connected) {
        if (update_.credentials_saved) {
            message = text("Connected and saved securely.", "Polaczono i bezpiecznie zapisano.");
            color = kAccent;
        } else {
            message = text("Connected, but credentials were not saved.",
                           "Polaczono, ale dane nie zostaly zapisane.");
            color = kError;
        }
    } else if (update_.state == wifi::ProvisioningState::Failed) {
        message = text("Check the password and signal, then try again.",
                       "Sprawdz haslo i zasieg, a potem sprobuj ponownie.");
        color = kError;
    } else {
        message = text("Connecting to the selected network...", "Laczenie z wybrana siecia...");
    }
    status_label_ = add_label(root_, 24, compact ? 96 : 136,
                              content_width, message, color, 24);

    if (update_.state == wifi::ProvisioningState::Connected &&
        update_.ipv4_address[0] != '\0') {
        char address[64]{};
        std::snprintf(address, sizeof(address), "IPv4: %s", update_.ipv4_address);
        add_label(root_, 24, compact ? 166 : 208, content_width, address, kMuted, 16);
    }

    if (update_.state == wifi::ProvisioningState::Failed) {
        add_button(root_, 24, compact ? 214 : 244, content_width, 58,
                   text("Try again", "Sprobuj ponownie"),
                   back_event, this);
        add_button(root_, 24, compact ? 282 : 314, content_width, 58,
                   text("Rescan networks", "Skanuj sieci ponownie"),
                   scan_event, this);
    } else if (update_.state == wifi::ProvisioningState::Connected) {
        add_button(root_, 24, compact ? 214 : 244, content_width, 58,
                   text("Choose another network", "Wybierz inna siec"),
                   scan_event, this);
    }
}

void WifiWizard::select_network(std::uint8_t index) {
    if (index >= update_.network_count) {
        return;
    }
    selected_network_ = update_.networks[index];
    if (selected_network_.secured) {
        page_ = Page::Password;
        render();
        return;
    }
    connect_selected();
}

void WifiWizard::connect_selected() {
    const char* password = password_field_ == nullptr
                               ? ""
                               : lv_textarea_get_text(password_field_);
    char password_copy[kMaxPasswordLength + 1]{};
    std::size_t length = 0;
    while (length < kMaxPasswordLength && password[length] != '\0') {
        password_copy[length] = password[length];
        ++length;
    }
    const esp_err_t status = provisioning_.connect(selected_network_.ssid, password_copy);
    std::memset(password_copy, 0, sizeof(password_copy));
    if (password_field_ != nullptr) {
        lv_textarea_set_text(password_field_, "");
        password_field_ = nullptr;
    }
    if (status == ESP_OK) {
        update_.state = wifi::ProvisioningState::Connecting;
        page_ = Page::Status;
    } else {
        update_.state = wifi::ProvisioningState::Failed;
        page_ = Page::Status;
    }
    render();
}

void WifiWizard::request_scan() {
    const esp_err_t status = provisioning_.request_scan();
    if (status != ESP_OK) {
        update_.state = wifi::ProvisioningState::Failed;
    } else {
        update_.state = wifi::ProvisioningState::Scanning;
    }
    page_ = Page::Networks;
    render();
}

void WifiWizard::poll_timer(lv_timer_t* timer) {
    auto* wizard = static_cast<WifiWizard*>(lv_timer_get_user_data(timer));
    if (wizard == nullptr) {
        return;
    }
    wifi::ProvisioningUpdate update{};
    const bool wifi_updated = wizard->provisioning_.receive_update(&update);
    if (wifi_updated) {
        wizard->update_ = update;
    }
    bool command_state_changed = false;
    radio::ZigbeeCoordinatorSnapshot zigbee_update{};
    const bool zigbee_read = wizard->zigbee_.read_snapshot(&zigbee_update);
    const bool zigbee_updated = zigbee_read &&
        (zigbee_update.available != wizard->zigbee_update_.available ||
         zigbee_update.stack_ready != wizard->zigbee_update_.stack_ready ||
         zigbee_update.network_formed != wizard->zigbee_update_.network_formed ||
         zigbee_update.search_pending != wizard->zigbee_update_.search_pending ||
         zigbee_update.device_count != wizard->zigbee_update_.device_count ||
         zigbee_update.pairing_seconds_remaining !=
             wizard->zigbee_update_.pairing_seconds_remaining ||
         zigbee_update.last_error != wizard->zigbee_update_.last_error ||
         zigbee_update.last_signal_type != wizard->zigbee_update_.last_signal_type ||
         zigbee_update.last_bdb_status != wizard->zigbee_update_.last_bdb_status);
    if (zigbee_read) {
        wizard->zigbee_update_ = zigbee_update;
        if (wizard->zigbee_request_pending_ &&
            (zigbee_update.search_pending ||
             (wizard->zigbee_network_creation_pending_ && zigbee_update.network_formed) ||
             (!wizard->zigbee_network_creation_pending_ &&
              zigbee_update.pairing_seconds_remaining > 0) ||
             zigbee_update.last_error != ESP_OK)) {
            wizard->zigbee_request_pending_ = false;
            wizard->zigbee_request_started_us_ = 0;
            command_state_changed = true;
        }
    }
    if (wizard->zigbee_request_pending_ &&
        esp_timer_get_time() - wizard->zigbee_request_started_us_ > 8000000) {
        wizard->zigbee_request_pending_ = false;
        wizard->zigbee_request_error_ = ESP_ERR_TIMEOUT;
        wizard->zigbee_request_started_us_ = 0;
        command_state_changed = true;
    }
    diagnostics::WebZigbeeRequestSnapshot web_request{};
    const bool web_request_read = wizard->web_approval_.read_snapshot(&web_request);
    const bool web_request_updated = web_request_read &&
        (web_request.id != wizard->web_request_.id ||
         web_request.state != wizard->web_request_.state ||
         web_request.result != wizard->web_request_.result);
    if (web_request_read) {
        wizard->web_request_ = web_request;
        if (web_request.state == diagnostics::WebZigbeeRequestState::awaiting_touch &&
            wizard->page_ != Page::Devices) {
            wizard->page_ = Page::Devices;
        }
    }
    if (web_request_read &&
        web_request.state == diagnostics::WebZigbeeRequestState::approved) {
        diagnostics::WebZigbeeAction action = diagnostics::WebZigbeeAction::none;
        if (wizard->web_approval_.claim(web_request.id, &action)) {
            esp_err_t status = ESP_ERR_INVALID_STATE;
            if (action == diagnostics::WebZigbeeAction::create_network) {
                status = wizard->zigbee_.request_network_start();
            } else if (action == diagnostics::WebZigbeeAction::open_join_window) {
                status = wizard->zigbee_.request_pairing_window(180);
            } else if (action == diagnostics::WebZigbeeAction::scan_nearby) {
                status = wizard->zigbee_.request_search();
            }
            (void)wizard->web_approval_.complete(web_request.id, status);
            (void)wizard->web_approval_.read_snapshot(&wizard->web_request_);
            command_state_changed = true;
        }
    }
    if (wizard->page_ == Page::Home) {
        if (wifi_updated || web_request_updated) {
            wizard->render();
        }
        return;
    }
    if ((wizard->page_ == Page::Devices &&
         (wifi_updated || zigbee_updated || command_state_changed || web_request_updated)) ||
        (wizard->page_ == Page::Diagnostics && wifi_updated)) {
        wizard->render();
        return;
    }
    if (!wifi_updated) {
        return;
    }
    if (wizard->page_ == Page::Password) {
        if (update.state == wifi::ProvisioningState::Connecting ||
            update.state == wifi::ProvisioningState::Connected ||
            update.state == wifi::ProvisioningState::Failed) {
            wizard->page_ = Page::Status;
            wizard->password_field_ = nullptr;
            wizard->render();
        }
        return;
    }
    if (update.state == wifi::ProvisioningState::Connecting ||
        update.state == wifi::ProvisioningState::Connected ||
        update.state == wifi::ProvisioningState::Failed) {
        wizard->page_ = Page::Status;
        wizard->password_field_ = nullptr;
    } else if (update.state == wifi::ProvisioningState::NetworksReady ||
               update.state == wifi::ProvisioningState::Scanning) {
        wizard->page_ = Page::Networks;
    }
    wizard->render();
}

void WifiWizard::language_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->polish_ = !wizard->polish_;
        wizard->render();
    }
}

void WifiWizard::scan_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->request_scan();
    }
}

void WifiWizard::access_point_event(lv_event_t* event) {
    auto* button = static_cast<AccessPointButton*>(lv_event_get_user_data(event));
    if (button != nullptr && button->wizard != nullptr) {
        button->wizard->select_network(button->index);
    }
}

void WifiWizard::show_password_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    lv_obj_t* checkbox = lv_event_get_target_obj(event);
    if (wizard != nullptr && wizard->password_field_ != nullptr) {
        lv_textarea_set_password_mode(wizard->password_field_,
                                      !lv_obj_has_state(checkbox, LV_STATE_CHECKED));
    }
}

void WifiWizard::connect_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->connect_selected();
    }
}

void WifiWizard::back_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    if (wizard->page_ == Page::Status &&
        wizard->update_.state == wifi::ProvisioningState::Failed) {
        if (!wizard->selected_network_.secured) {
            wizard->connect_selected();
            return;
        }
        wizard->page_ = Page::Password;
        wizard->update_.state = wifi::ProvisioningState::NetworksReady;
    } else {
        wizard->page_ = Page::Networks;
    }
    wizard->password_field_ = nullptr;
    wizard->render();
}

void WifiWizard::home_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->page_ = Page::Home;
        wizard->password_field_ = nullptr;
        wizard->render();
    }
}

void WifiWizard::diagnostics_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->page_ = Page::Diagnostics;
        wizard->render();
    }
}

void WifiWizard::diagnostics_refresh_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wifi::ProvisioningUpdate update{};
        if (wizard->provisioning_.receive_update(&update)) {
            wizard->update_ = update;
        }
        wizard->render();
    }
}

void WifiWizard::devices_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard != nullptr) {
        wizard->page_ = Page::Devices;
        wizard->network_start_confirmation_pending_ = false;
        wizard->pairing_confirmation_pending_ = false;
        wizard->zigbee_request_pending_ = false;
        wizard->zigbee_request_error_ = ESP_OK;
        wizard->zigbee_request_started_us_ = 0;
        wizard->render();
    }
}

void WifiWizard::network_start_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    if (!wizard->network_start_confirmation_pending_) {
        wizard->network_start_confirmation_pending_ = true;
    } else {
        wizard->network_start_confirmation_pending_ = false;
        const esp_err_t status = wizard->zigbee_.request_network_start();
        wizard->zigbee_request_error_ = status;
        wizard->zigbee_request_pending_ = status == ESP_OK;
        wizard->zigbee_network_creation_pending_ = status == ESP_OK;
        wizard->zigbee_request_started_us_ = status == ESP_OK ? esp_timer_get_time() : 0;
    }
    wizard->render();
}

void WifiWizard::pairing_window_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    if (!wizard->pairing_confirmation_pending_) {
        wizard->pairing_confirmation_pending_ = true;
    } else {
        wizard->pairing_confirmation_pending_ = false;
        const esp_err_t status = wizard->zigbee_.request_pairing_window(180);
        wizard->zigbee_request_error_ = status;
        wizard->zigbee_request_pending_ = status == ESP_OK;
        wizard->zigbee_network_creation_pending_ = false;
        wizard->zigbee_request_started_us_ = status == ESP_OK ? esp_timer_get_time() : 0;
    }
    wizard->render();
}

void WifiWizard::scan_nearby_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    if (!wizard->scan_nearby_confirmation_pending_) {
        wizard->scan_nearby_confirmation_pending_ = true;
    } else {
        wizard->scan_nearby_confirmation_pending_ = false;
        const esp_err_t status = wizard->zigbee_.request_search();
        wizard->zigbee_request_error_ = status;
        wizard->zigbee_request_pending_ = status == ESP_OK;
        wizard->zigbee_network_creation_pending_ = !wizard->zigbee_update_.network_formed;
        wizard->zigbee_request_started_us_ = status == ESP_OK ? esp_timer_get_time() : 0;
    }
    wizard->render();
}

void WifiWizard::web_approval_confirm_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    diagnostics::WebZigbeeAction action = diagnostics::WebZigbeeAction::none;
    if (wizard->web_approval_.claim(wizard->web_request_.id, &action)) {
        esp_err_t status = ESP_ERR_INVALID_STATE;
        if (action == diagnostics::WebZigbeeAction::create_network) {
            status = wizard->zigbee_.request_network_start();
        } else if (action == diagnostics::WebZigbeeAction::open_join_window) {
            status = wizard->zigbee_.request_pairing_window(180);
        } else if (action == diagnostics::WebZigbeeAction::scan_nearby) {
            status = wizard->zigbee_.request_search();
        }
        (void)wizard->web_approval_.complete(wizard->web_request_.id, status);
        wizard->web_approval_.read_snapshot(&wizard->web_request_);
    }
    wizard->render();
}

void WifiWizard::web_approval_reject_event(lv_event_t* event) {
    auto* wizard = static_cast<WifiWizard*>(lv_event_get_user_data(event));
    if (wizard == nullptr) {
        return;
    }
    (void)wizard->web_approval_.reject(wizard->web_request_.id);
    wizard->web_approval_.read_snapshot(&wizard->web_request_);
    wizard->render();
}

}