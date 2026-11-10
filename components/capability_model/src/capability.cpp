#include "hub/capability.hpp"

namespace hub {

bool is_valid_value(CapabilityKey key, const CapabilityValue& value) noexcept {
    switch (key) {
        case CapabilityKey::switch_on:
        case CapabilityKey::contact:
            return std::holds_alternative<bool>(value);
        case CapabilityKey::light_level:
            return std::holds_alternative<std::uint8_t>(value) &&
                   std::get<std::uint8_t>(value) <= 254;
        case CapabilityKey::temperature_centi_c:
            return std::holds_alternative<std::int16_t>(value);
        case CapabilityKey::humidity_centi_percent:
            return std::holds_alternative<std::uint16_t>(value) &&
                   std::get<std::uint16_t>(value) <= 10000;
        case CapabilityKey::battery_percent:
            return std::holds_alternative<std::uint8_t>(value) &&
                   std::get<std::uint8_t>(value) <= 100;
        case CapabilityKey::battery_voltage_millivolts:
            return std::holds_alternative<std::uint16_t>(value);
    }

    return false;
}

}