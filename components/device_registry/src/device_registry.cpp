#include "hub/device_registry.hpp"

namespace hub {
namespace {

bool is_zero(const DeviceId& id) noexcept {
    for (const auto byte : id) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

bool matches(const ZigbeeIdentity& left, const ZigbeeIdentity& right) noexcept {
    return left.coordinator_id == right.coordinator_id &&
           left.ieee_address == right.ieee_address;
}

}

DeviceRegistry::DeviceRegistry(RegisteredDevice* storage, std::size_t capacity) noexcept
    : storage_(storage), capacity_(storage == nullptr ? 0 : capacity) {}

RegistryResult DeviceRegistry::add(const DeviceId& id, const ZigbeeIdentity& identity) noexcept {
    if (is_zero(id)) {
        return RegistryResult::invalid_device_id;
    }

    if (find_by_id(id) != nullptr) {
        return RegistryResult::duplicate_device_id;
    }

    if (find_by_zigbee_identity(identity) != nullptr) {
        return RegistryResult::duplicate_zigbee_identity;
    }

    if (size_ == capacity_) {
        return RegistryResult::capacity_exhausted;
    }

    storage_[size_++] = RegisteredDevice{id, identity, false};
    return RegistryResult::added;
}

const RegisteredDevice* DeviceRegistry::find_by_id(const DeviceId& id) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (storage_[index].id == id) {
            return &storage_[index];
        }
    }
    return nullptr;
}

const RegisteredDevice* DeviceRegistry::find_by_zigbee_identity(
    const ZigbeeIdentity& identity) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (matches(storage_[index].zigbee, identity)) {
            return &storage_[index];
        }
    }
    return nullptr;
}

bool DeviceRegistry::set_reachable(const DeviceId& id, bool reachable) noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (storage_[index].id == id) {
            storage_[index].reachable = reachable;
            return true;
        }
    }
    return false;
}

bool DeviceRegistry::remove(const DeviceId& id) noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
        if (storage_[index].id == id) {
            for (std::size_t next = index + 1; next < size_; ++next) {
                storage_[next - 1] = storage_[next];
            }
            --size_;
            return true;
        }
    }
    return false;
}

std::size_t DeviceRegistry::size() const noexcept {
    return size_;
}

std::size_t DeviceRegistry::capacity() const noexcept {
    return capacity_;
}

}