#pragma once

#include <cstdint>

namespace hub::radio {

inline constexpr std::uint16_t kZigbeeLevelCurrentAttributeId = 0x0000;
inline constexpr std::uint32_t kZigbeeLevelFreshnessMs = 10000;

struct ZigbeeLevelState {
    bool known = false;
    std::uint8_t value = 0;
    bool read_pending = false;
    std::uint32_t updated_at_ms = 0;
};

inline bool zigbee_level_state_is_fresh(
    const ZigbeeLevelState& state, std::uint32_t now_ms) noexcept {
    return state.known && now_ms - state.updated_at_ms <=
        kZigbeeLevelFreshnessMs;
}

inline bool zigbee_level_read_is_due(
    const ZigbeeLevelState& state, std::uint32_t now_ms) noexcept {
    return !state.read_pending &&
        !zigbee_level_state_is_fresh(state, now_ms);
}

inline void mark_zigbee_level_read_pending(
    ZigbeeLevelState& state) noexcept {
    state.read_pending = true;
}

inline bool apply_zigbee_level_attribute(
    ZigbeeLevelState& state, std::uint16_t cluster_id,
    std::uint16_t attribute_id, std::uint8_t status,
    std::uint8_t value_type, std::uint8_t value,
    std::uint8_t success_status, std::uint8_t uint8_type,
    std::uint32_t now_ms) noexcept {
    if (cluster_id != 0x0008 ||
        attribute_id != kZigbeeLevelCurrentAttributeId) {
        return false;
    }
    state.read_pending = false;
    if (status != success_status || value_type != uint8_type || value > 254) {
        state.known = false;
        return false;
    }
    state.known = true;
    state.value = value;
    state.updated_at_ms = now_ms;
    return true;
}

inline void fail_zigbee_level_read(ZigbeeLevelState& state) noexcept {
    state.read_pending = false;
    state.known = false;
}

}  // namespace hub::radio