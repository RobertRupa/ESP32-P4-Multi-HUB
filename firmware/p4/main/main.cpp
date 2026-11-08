#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_log.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "board_hmi.hpp"
#include "c6_ota.hpp"
#include "diagnostics_http.hpp"
#include "multipan_service.hpp"
#include "wifi_provisioning.hpp"
#include "wifi_wizard.hpp"
#include "uart_diagnostic.hpp"
#include "c6_zigbee_bridge.hpp"

#include <cstdio>
#include <cstring>

namespace {

constexpr char kTag[] = "p4_hosted_host";
constexpr char kMdnsHostname[] = "MultiHub";
constexpr char kWebPasswordResetConfirmation[] =
    "CONFIRM-WEB-PASSWORD-RESET";
hub::diagnostics::WebSecurity* web_security_for_console = nullptr;

void start_mdns_services() {
    esp_err_t status = mdns_init();
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "mDNS initialization failed: %s", esp_err_to_name(status));
        return;
    }
    status = mdns_hostname_set(kMdnsHostname);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "mDNS hostname setup failed: %s", esp_err_to_name(status));
        return;
    }
    status = mdns_instance_name_set(kMdnsHostname);
    if (status != ESP_OK) {
        ESP_LOGW(kTag, "mDNS instance setup failed: %s", esp_err_to_name(status));
    }
    const esp_err_t http_status = mdns_service_add(
        kMdnsHostname, "_http", "_tcp", 80, nullptr, 0);
    const esp_err_t https_status = mdns_service_add(
        kMdnsHostname, "_https", "_tcp", 443, nullptr, 0);
    if (http_status != ESP_OK || https_status != ESP_OK) {
        ESP_LOGW(kTag, "mDNS service registration failed: http=%s https=%s",
                 esp_err_to_name(http_status), esp_err_to_name(https_status));
        return;
    }
    ESP_LOGI(kTag, "mDNS hostname: %s.local", kMdnsHostname);
}

int web_password_reset_command(int argc, char** argv) {
    if (argc != 2 || argv == nullptr ||
        std::strcmp(argv[1], kWebPasswordResetConfirmation) != 0) {
        std::printf("Usage: web_password_reset %s\n",
                    kWebPasswordResetConfirmation);
        return 1;
    }
    if (web_security_for_console == nullptr) {
        std::printf("Web security service is unavailable\n");
        return 1;
    }
    char one_time_password[33]{};
    const esp_err_t status = web_security_for_console->reset_password(
        one_time_password, sizeof(one_time_password));
    if (status != ESP_OK) {
        std::printf("Web password reset failed: %s\n", esp_err_to_name(status));
        std::memset(one_time_password, 0, sizeof(one_time_password));
        return 1;
    }
    std::printf("One-time Web UI password for admin (change immediately): %s\n",
                one_time_password);
    std::printf("Password change is required before Zigbee actions.\n");
    std::memset(one_time_password, 0, sizeof(one_time_password));
    return 0;
}

void start_console(hub::radio::C6ZigbeeBridge* c6_zigbee_bridge,
                   hub::diagnostics::WebSecurity& web_security) {
    esp_console_dev_usb_serial_jtag_config_t device_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "p4-hosted>";

    esp_console_repl_t* repl = nullptr;
    esp_err_t status = esp_console_new_repl_usb_serial_jtag(
        &device_config, &repl_config, &repl);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "USB Serial/JTAG REPL init failed: %s", esp_err_to_name(status));
        return;
    }
    status = hub::register_c6_ota_command();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 OTA command registration failed: %s", esp_err_to_name(status));
        return;
    }
    status = hub::register_c6_diagnostics_command();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "C6 diagnostics command registration failed: %s",
                 esp_err_to_name(status));
        return;
    }
    if (c6_zigbee_bridge != nullptr) {
        status = hub::radio::register_c6_zigbee_console(*c6_zigbee_bridge);
        if (status != ESP_OK) {
            ESP_LOGE(kTag, "C6 Zigbee console registration failed: %s",
                     esp_err_to_name(status));
            return;
        }
    }
    web_security_for_console = &web_security;
    esp_console_cmd_t password_reset_command{};
    password_reset_command.command = "web_password_reset";
    password_reset_command.help =
        "Create a fresh one-time Web UI password; requires the exact confirmation token";
    password_reset_command.hint = "CONFIRM-WEB-PASSWORD-RESET";
    password_reset_command.func = web_password_reset_command;
    status = esp_console_cmd_register(&password_reset_command);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "Web password reset command registration failed: %s",
                 esp_err_to_name(status));
        return;
    }
    status = esp_console_start_repl(repl);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "USB Serial/JTAG REPL start failed: %s", esp_err_to_name(status));
    }
}

}

extern "C" void app_main(void) {
#if CONFIG_HUB_UART_DIAGNOSTIC_ENABLE
    hub::diagnostics::run_uart_diagnostic();
    return;
#endif

    const esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %d; persistent storage was not erased",
                 static_cast<int>(nvs_result));
        return;
    }

    const esp_err_t netif_result = esp_netif_init();
    if (netif_result != ESP_OK) {
        ESP_LOGE(kTag, "Network interface init failed: %d",
                 static_cast<int>(netif_result));
        return;
    }

    const esp_err_t event_result = esp_event_loop_create_default();
    if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "Default event loop init failed: %d",
                 static_cast<int>(event_result));
        return;
    }

    const int hosted_result = esp_hosted_init();
    if (hosted_result != 0) {
        ESP_LOGE(kTag, "ESP-Hosted init failed: %d", hosted_result);
        return;
    }

    esp_hosted_coprocessor_fwver_t coprocessor_version{};
    const int version_result =
        esp_hosted_get_coprocessor_fwversion(&coprocessor_version);
    if (version_result == 0) {
        ESP_LOGI(kTag, "ESP-Hosted coprocessor firmware: %u.%u.%u",
                 static_cast<unsigned>(coprocessor_version.major1),
                 static_cast<unsigned>(coprocessor_version.minor1),
                 static_cast<unsigned>(coprocessor_version.patch1));
        hub::verify_c6_ota_after_boot(coprocessor_version.major1,
                                      coprocessor_version.minor1,
                                      coprocessor_version.patch1);
    } else {
        ESP_LOGW(kTag, "Coprocessor firmware version query failed: %d",
                 version_result);
    }

    uint32_t coprocessor_chip_id = 0;
    char coprocessor_target[32]{};
    const int info_result = esp_hosted_get_cp_info(
        &coprocessor_chip_id, coprocessor_target, sizeof(coprocessor_target));
    if (info_result != 0) {
        ESP_LOGW(kTag, "Coprocessor target information unavailable: %d", info_result);
    } else if (coprocessor_chip_id == 0 && coprocessor_target[0] == '\0') {
        ESP_LOGW(kTag, "Coprocessor target metadata is not provided by its firmware");
    } else {
        ESP_LOGI(kTag, "ESP-Hosted coprocessor; target=%s chip_id=0x%08lx",
                 coprocessor_target, static_cast<unsigned long>(coprocessor_chip_id));
    }

    esp_chip_info_t host_chip_info{};
    esp_chip_info(&host_chip_info);
    ESP_LOGI(kTag, "ESP32-P4 host ready; revision=%d cores=%d",
             host_chip_info.revision, host_chip_info.cores);

    static hub::wifi::Provisioning wifi_provisioning;
    static hub::radio::MultipanService multipan_service(wifi_provisioning);
    static hub::radio::C6ZigbeeBridge c6_zigbee_bridge;
    static hub::diagnostics::WebSecurity web_security;
    static hub::diagnostics::WebZigbeeApproval web_zigbee_approval;
    hub::radio::IZigbeeCoordinator* zigbee_coordinator = nullptr;
    hub::radio::C6ZigbeeBridge* c6_zigbee_console = nullptr;
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
    zigbee_coordinator = &multipan_service;
#else
    zigbee_coordinator = &c6_zigbee_bridge;
    c6_zigbee_console = &c6_zigbee_bridge;
#endif
    const esp_err_t wifi_status = wifi_provisioning.start();
    if (wifi_status != ESP_OK) {
        ESP_LOGE(kTag, "Wi-Fi provisioning service failed to start: %s",
                 esp_err_to_name(wifi_status));
    } else {
#if CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE
        const esp_err_t zigbee_status = multipan_service.start();
        hub::radio::MultipanSnapshot rcp_snapshot{};
        if (multipan_service.read_snapshot(&rcp_snapshot) &&
            rcp_snapshot.state == hub::radio::MultipanState::needs_uart_configuration) {
            ESP_LOGE(kTag, "H2 Zigbee RCP is disabled until UART pins are configured");
        }
#else
        const esp_err_t zigbee_status = c6_zigbee_bridge.start();
        const esp_err_t multipan_status = multipan_service.start();
        if (multipan_status != ESP_OK) {
            ESP_LOGW(kTag, "Multipan service unavailable: %s",
                     esp_err_to_name(multipan_status));
        }
#endif
        if (zigbee_status != ESP_OK) {
            ESP_LOGW(kTag, "Zigbee coordinator unavailable: %s",
                     esp_err_to_name(zigbee_status));
        }
        const esp_err_t http_status = hub::diagnostics::start_http(
            wifi_provisioning, multipan_service, *zigbee_coordinator,
            web_security, web_zigbee_approval);
        if (http_status != ESP_OK) {
            ESP_LOGE(kTag, "HTTP diagnostics failed to start: %s",
                     esp_err_to_name(http_status));
        } else {
            start_mdns_services();
        }
    }

    lv_display_t* display = nullptr;
    const esp_err_t display_status = hub::board::init_hmi(&display);
    if (display_status != ESP_OK) {
        ESP_LOGE(kTag, "LCD/touch initialization failed: %s",
                 esp_err_to_name(display_status));
    } else if (wifi_status == ESP_OK) {
        static hub::ui::WifiWizard wifi_wizard(
            wifi_provisioning, *zigbee_coordinator, web_zigbee_approval);
        const esp_err_t wizard_status = wifi_wizard.start(display);
        if (wizard_status != ESP_OK) {
            ESP_LOGE(kTag, "Wi-Fi wizard failed to start: %s",
                     esp_err_to_name(wizard_status));
        }
    }
    start_console(c6_zigbee_console, web_security);
}