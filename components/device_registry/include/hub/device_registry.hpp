#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hub {

using DeviceId = std::array<std::uint8_t, 16>;

struct ZigbeeIdentity {
    std::array<std::uint8_t, 8> coordinator_id;
    std::array<std::uint8_t, 8> ieee_address;
};

struct RegisteredDevice {
    DeviceId id;
    ZigbeeIdentity zigbee;
    bool reachable;
};

enum class RegistryResult : std::uint8_t {
    added,
    invalid_device_id,
    duplicate_device_id,
    duplicate_zigbee_identity,
    capacity_exhausted,
};

class DeviceRegistry {
public:
    DeviceRegistry(RegisteredDevice* storage, std::size_t capacity) noexcept;

    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    RegistryResult add(const DeviceId& id, const ZigbeeIdentity& identity) noexcept;
    const RegisteredDevice* find_by_id(const DeviceId& id) const noexcept;
    const RegisteredDevice* find_by_zigbee_identity(const ZigbeeIdentity& identity) const noexcept;
    bool set_reachable(const DeviceId& id, bool reachable) noexcept;
    bool remove(const DeviceId& id) noexcept;
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;

private:
    RegisteredDevice* storage_;
    std::size_t capacity_;
    std::size_t size_ = 0;
};

}