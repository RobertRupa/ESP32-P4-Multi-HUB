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

}