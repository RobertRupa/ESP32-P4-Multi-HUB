#include "zigbee_search_policy.hpp"

#include <cassert>

int main() {
    using hub::radio::ZigbeeSearchAction;
    using hub::radio::select_zigbee_search_action;

    assert(select_zigbee_search_action(false, false) ==
           ZigbeeSearchAction::unavailable);
    assert(select_zigbee_search_action(false, true) ==
           ZigbeeSearchAction::unavailable);
    assert(select_zigbee_search_action(true, false) ==
           ZigbeeSearchAction::form_and_open_join_window);
    assert(select_zigbee_search_action(true, true) ==
           ZigbeeSearchAction::open_join_window);
    return 0;
}