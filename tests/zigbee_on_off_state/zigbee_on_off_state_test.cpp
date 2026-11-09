#include "zigbee_on_off_state.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using hub::radio::ZigbeeOnOffState;
    using hub::radio::apply_zigbee_on_off_attribute;
    using hub::radio::fail_zigbee_on_off_read;
    using hub::radio::mark_zigbee_on_off_read_pending;
    using hub::radio::zigbee_on_off_read_is_due;
    using hub::radio::zigbee_on_off_state_is_fresh;

    ZigbeeOnOffState state{};
    assert(!state.known);
    assert(zigbee_on_off_read_is_due(state, 1000));

    mark_zigbee_on_off_read_pending(state);
    assert(!zigbee_on_off_read_is_due(state, 1000));
    assert(apply_zigbee_on_off_attribute(
        state, 0x0006, 0x0000, 0, 0x10, 1, 0, 0x10, 1000));
    assert(state.known && state.value && !state.read_pending);
    assert(zigbee_on_off_state_is_fresh(state, 11000));
    assert(!zigbee_on_off_read_is_due(state, 11000));
    assert(zigbee_on_off_read_is_due(state, 11001));

    assert(!apply_zigbee_on_off_attribute(
        state, 0x0008, 0x0000, 0, 0x10, 0, 0, 0x10, 17000));
    assert(state.known && state.value);
    assert(!apply_zigbee_on_off_attribute(
        state, 0x0006, 0x0001, 0, 0x10, 0, 0, 0x10, 17000));
    assert(state.known && state.value);

    mark_zigbee_on_off_read_pending(state);
    assert(!apply_zigbee_on_off_attribute(
        state, 0x0006, 0x0000, 0x86, 0x10, 0, 0, 0x10, 18000));
    assert(!state.known && !state.read_pending);

    mark_zigbee_on_off_read_pending(state);
    assert(apply_zigbee_on_off_attribute(
        state, 0x0006, 0x0000, 0, 0x10, 0, 0, 0x10, 19000));
    assert(state.known && !state.value);

    mark_zigbee_on_off_read_pending(state);
    fail_zigbee_on_off_read(state);
    assert(!state.known && !state.read_pending);
    return 0;
}