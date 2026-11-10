#include "zigbee_search_policy.hpp"

#include <cassert>

int main() {
    using hub::radio::ZigbeeSearchAction;
       using hub::radio::finish_zigbee_formation;
    using hub::radio::select_zigbee_search_action;

    assert(select_zigbee_search_action(false, false) ==
           ZigbeeSearchAction::unavailable);
    assert(select_zigbee_search_action(false, true) ==
           ZigbeeSearchAction::unavailable);
    assert(select_zigbee_search_action(true, false) ==
           ZigbeeSearchAction::form_and_open_join_window);
    assert(select_zigbee_search_action(true, true) ==
           ZigbeeSearchAction::open_join_window);

    const auto formed = finish_zigbee_formation(true, false, true);
    assert(formed.network_formed);
    assert(!formed.search_pending);
    assert(!formed.formation_timed_out);
    assert(formed.open_join_window);

    const auto failed = finish_zigbee_formation(false, false, true);
    assert(!failed.network_formed);
    assert(!failed.search_pending);
    assert(!failed.formation_timed_out);
    assert(!failed.open_join_window);

    const auto timed_out = finish_zigbee_formation(false, true, true);
    assert(!timed_out.network_formed);
    assert(!timed_out.search_pending);
    assert(timed_out.formation_timed_out);
    assert(!timed_out.open_join_window);
    return 0;
}