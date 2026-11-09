#pragma once

#include <cstdint>

namespace hub::radio {

inline constexpr std::uint16_t kZigbeeOnOffStateAttributeId = 0x0000;
inline constexpr std::uint32_t kZigbeeOnOffFreshnessMs = 10000;

struct ZigbeeOnOffState {
    bool known = false;
    bool value = false;
    bool read_pending = false;
    std::uint32_t updated_at_ms = 0;
};

inline bool zigbee_on_off_state_is_fresh(
    const ZigbeeOnOffState& state, std::uint32_t now_ms) noexcept {
    return state.known && now_ms - state.updated_at_ms <=
        kZigbeeOnOffFreshnessMs;
}

inline bool zigbee_on_off_read_is_due(
    const ZigbeeOnOffState& state, std::uint32_t now_ms) noexcept {
    return !state.read_pending &&
        !zigbee_on_off_state_is_fresh(state, now_ms);
}

inline void mark_zigbee_on_off_read_pending(
    ZigbeeOnOffState& state) noexcept {
    state.read_pending = true;
}

inline bool apply_zigbee_on_off_attribute(
    ZigbeeOnOffState& state, std::uint16_t cluster_id,
    std::uint16_t attribute_id, std::uint8_t status,
    std::uint8_t value_type, std::uint8_t value,
    std::uint8_t success_status, std::uint8_t boolean_type,
    std::uint32_t now_ms) noexcept {
    if (cluster_id != 0x0006 ||
        attribute_id != kZigbeeOnOffStateAttributeId) {
        return false;
    }
    state.read_pending = false;
    if (status != success_status || value_type != boolean_type || value > 1) {
        state.known = false;
        return false;
    }
    state.known = true;
    state.value = value != 0;
    state.updated_at_ms = now_ms;
    return true;
}

inline void fail_zigbee_on_off_read(ZigbeeOnOffState& state) noexcept {
    state.read_pending = false;
    state.known = false;
}

}