#include "zigbee_level_state.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using hub::radio::ZigbeeLevelState;
    using hub::radio::apply_zigbee_level_attribute;
    using hub::radio::fail_zigbee_level_read;
    using hub::radio::mark_zigbee_level_read_pending;
    using hub::radio::zigbee_level_read_is_due;
    using hub::radio::zigbee_level_state_is_fresh;

    ZigbeeLevelState state{};
    assert(!state.known);
    assert(zigbee_level_read_is_due(state, 1000));

    mark_zigbee_level_read_pending(state);
    assert(!zigbee_level_read_is_due(state, 1000));
    assert(apply_zigbee_level_attribute(
        state, 0x0008, 0x0000, 0, 0x20, 93, 0, 0x20, 1000));
    assert(state.known && state.value == 93 && !state.read_pending);
    assert(zigbee_level_state_is_fresh(state, 11000));
    assert(!zigbee_level_read_is_due(state, 11000));
    assert(zigbee_level_read_is_due(state, 11001));

    assert(!apply_zigbee_level_attribute(
        state, 0x0006, 0x0000, 0, 0x20, 50, 0, 0x20, 12000));
    assert(state.known && state.value == 93);
    assert(!apply_zigbee_level_attribute(
        state, 0x0008, 0x0001, 0, 0x20, 50, 0, 0x20, 12000));
    assert(state.known && state.value == 93);

    mark_zigbee_level_read_pending(state);
    assert(!apply_zigbee_level_attribute(
        state, 0x0008, 0x0000, 0x86, 0x20, 50, 0, 0x20, 13000));
    assert(!state.known && !state.read_pending);

    mark_zigbee_level_read_pending(state);
    assert(!apply_zigbee_level_attribute(
        state, 0x0008, 0x0000, 0, 0x20, 0xFF, 0, 0x20, 14000));
    assert(!state.known && !state.read_pending);

    mark_zigbee_level_read_pending(state);
    assert(apply_zigbee_level_attribute(
        state, 0x0008, 0x0000, 0, 0x20, 0, 0, 0x20, 15000));
    assert(state.known && state.value == 0);

    mark_zigbee_level_read_pending(state);
    fail_zigbee_level_read(state);
    assert(!state.known && !state.read_pending);
    return 0;
}