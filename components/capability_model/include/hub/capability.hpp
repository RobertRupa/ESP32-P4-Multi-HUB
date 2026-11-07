#pragma once

#include <cstdint>
#include <variant>

namespace hub {

enum class CapabilityKey : std::uint8_t {
    switch_on,
    light_level,
    temperature_centi_c,
    humidity_centi_percent,
    contact,
    battery_percent,
    battery_voltage_millivolts,
};

using CapabilityValue = std::variant<bool, std::uint8_t, std::int16_t, std::uint16_t>;

bool is_valid_value(CapabilityKey key, const CapabilityValue& value) noexcept;

}