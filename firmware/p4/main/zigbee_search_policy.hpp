#pragma once

#include <cstdint>

namespace hub::radio {

enum class ZigbeeSearchAction : std::uint8_t {
    unavailable,
    form_and_open_join_window,
    open_join_window,
};

constexpr ZigbeeSearchAction select_zigbee_search_action(
    bool stack_ready, bool network_formed) noexcept {
    if (!stack_ready) {
        return ZigbeeSearchAction::unavailable;
    }
    return network_formed
        ? ZigbeeSearchAction::open_join_window
        : ZigbeeSearchAction::form_and_open_join_window;
}

struct ZigbeeFormationOutcome {
    bool network_formed = false;
    bool search_pending = false;
    bool formation_timed_out = false;
    bool open_join_window = false;
};

constexpr ZigbeeFormationOutcome finish_zigbee_formation(
    bool succeeded, bool timed_out, bool open_join_after) noexcept {
    return {succeeded, false, timed_out, succeeded && open_join_after};
}

}