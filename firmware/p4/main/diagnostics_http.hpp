#pragma once

#include "esp_err.h"
#include "multipan_service.hpp"
#include "wifi_provisioning.hpp"
#include "zigbee_coordinator.hpp"
#include "web_security.hpp"
#include "web_zigbee_approval.hpp"

namespace hub::diagnostics {

esp_err_t start_http(wifi::Provisioning& provisioning,
					 radio::MultipanService& multipan,
						 radio::IZigbeeCoordinator& zigbee,
					 WebSecurity& security,
					 WebZigbeeApproval& approval) noexcept;

}
