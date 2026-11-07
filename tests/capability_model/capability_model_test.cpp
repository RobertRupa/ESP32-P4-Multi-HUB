#include "hub/capability.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using hub::CapabilityKey;
    using hub::CapabilityValue;

    assert(hub::is_valid_value(CapabilityKey::switch_on, CapabilityValue{true}));
    assert(!hub::is_valid_value(CapabilityKey::switch_on, CapabilityValue{std::uint8_t{1}}));
    assert(hub::is_valid_value(CapabilityKey::light_level, CapabilityValue{std::uint8_t{0}}));
    assert(hub::is_valid_value(CapabilityKey::light_level, CapabilityValue{std::uint8_t{254}}));
    assert(!hub::is_valid_value(CapabilityKey::light_level, CapabilityValue{std::uint8_t{255}}));
    assert(hub::is_valid_value(CapabilityKey::temperature_centi_c, CapabilityValue{std::int16_t{-1250}}));
    assert(hub::is_valid_value(CapabilityKey::humidity_centi_percent, CapabilityValue{std::uint16_t{10000}}));
    assert(!hub::is_valid_value(CapabilityKey::humidity_centi_percent, CapabilityValue{std::uint16_t{10001}}));
    assert(hub::is_valid_value(CapabilityKey::contact, CapabilityValue{false}));
    assert(hub::is_valid_value(CapabilityKey::battery_percent, CapabilityValue{std::uint8_t{100}}));
    assert(!hub::is_valid_value(CapabilityKey::battery_percent, CapabilityValue{std::uint8_t{101}}));
    assert(hub::is_valid_value(CapabilityKey::battery_voltage_millivolts, CapabilityValue{std::uint16_t{3000}}));
    assert(!hub::is_valid_value(CapabilityKey::battery_voltage_millivolts, CapabilityValue{std::uint8_t{30}}));
}