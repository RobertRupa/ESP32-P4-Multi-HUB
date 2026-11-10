#include "hub/device_registry.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
    using hub::DeviceId;
    using hub::DeviceRegistry;
    using hub::RegisteredDevice;
    using hub::RegistryResult;
    using hub::ZigbeeIdentity;

    std::array<RegisteredDevice, 2> storage{};
    DeviceRegistry registry(storage.data(), storage.size());

    DeviceId first_id{};
    first_id[0] = 1;
    DeviceId second_id{};
    second_id[0] = 2;
    DeviceId third_id{};
    third_id[0] = 3;

    const ZigbeeIdentity first_identity{{1, 2, 3, 4, 5, 6, 7, 8},
                                        {8, 7, 6, 5, 4, 3, 2, 1}};
    const ZigbeeIdentity second_identity{{1, 2, 3, 4, 5, 6, 7, 8},
                                         {1, 1, 1, 1, 1, 1, 1, 1}};

    assert(registry.size() == 0);
    assert(registry.capacity() == 2);
    assert(registry.add(DeviceId{}, first_identity) == RegistryResult::invalid_device_id);
    assert(registry.add(first_id, first_identity) == RegistryResult::added);
    assert(registry.add(first_id, second_identity) == RegistryResult::duplicate_device_id);
    assert(registry.add(second_id, first_identity) == RegistryResult::duplicate_zigbee_identity);
    assert(registry.find_by_id(first_id) != nullptr);
    assert(registry.find_by_zigbee_identity(first_identity)->id == first_id);
    assert(!registry.find_by_id(first_id)->reachable);

    assert(registry.set_reachable(first_id, true));
    assert(registry.find_by_id(first_id)->reachable);
    assert(!registry.set_reachable(third_id, true));

    assert(registry.add(second_id, second_identity) == RegistryResult::added);
    assert(registry.add(third_id, ZigbeeIdentity{}) == RegistryResult::capacity_exhausted);
    assert(registry.remove(first_id));
    assert(registry.size() == 1);
    assert(registry.find_by_id(first_id) == nullptr);
    assert(registry.find_by_id(second_id) != nullptr);
    assert(!registry.remove(first_id));
}