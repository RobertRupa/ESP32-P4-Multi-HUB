#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hub::radio {

inline constexpr std::uint8_t kZigbeeKnownDevicesSchemaVersion = 1;
inline constexpr std::size_t kMaxPersistedZigbeeShortAddresses = 16;
inline constexpr std::size_t kZigbeeKnownDevicesBlobCapacity =
    4 + 2 * kMaxPersistedZigbeeShortAddresses + 4;

struct ZigbeeKnownDevices {
    std::array<std::uint16_t, kMaxPersistedZigbeeShortAddresses> addresses{};
    std::uint8_t count = 0;

    bool contains(std::uint16_t address) const noexcept {
        for (std::size_t index = 0; index < count; ++index) {
            if (addresses[index] == address) {
                return true;
            }
        }
        return false;
    }

    bool add(std::uint16_t address) noexcept {
        if (address == 0 || address == 0xFFFFU) {
            return false;
        }
        if (contains(address)) {
            return true;
        }
        if (count >= addresses.size()) {
            return false;
        }
        addresses[count++] = address;
        return true;
    }
};

inline std::uint32_t zigbee_known_devices_crc32(
    const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

inline bool encode_zigbee_known_devices(
    const ZigbeeKnownDevices& devices,
    std::array<std::uint8_t, kZigbeeKnownDevicesBlobCapacity>* output,
    std::size_t* output_size) noexcept {
    if (output == nullptr || output_size == nullptr ||
        devices.count > devices.addresses.size()) {
        return false;
    }
    *output = {};
    (*output)[0] = 'Z';
    (*output)[1] = 'K';
    (*output)[2] = kZigbeeKnownDevicesSchemaVersion;
    (*output)[3] = devices.count;
    for (std::size_t index = 0; index < devices.count; ++index) {
        const std::uint16_t address = devices.addresses[index];
        if (address == 0 || address == 0xFFFFU) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (devices.addresses[previous] == address) {
                return false;
            }
        }
        const std::size_t offset = 4 + index * 2;
        (*output)[offset] = static_cast<std::uint8_t>(address & 0xFFU);
        (*output)[offset + 1] = static_cast<std::uint8_t>(address >> 8U);
    }
    const std::size_t payload_size = 4 + devices.count * 2;
    const std::uint32_t crc = zigbee_known_devices_crc32(output->data(), payload_size);
    for (std::size_t index = 0; index < 4; ++index) {
        (*output)[payload_size + index] =
            static_cast<std::uint8_t>(crc >> (index * 8U));
    }
    *output_size = payload_size + 4;
    return true;
}

inline bool decode_zigbee_known_devices(
    const std::uint8_t* data, std::size_t size,
    ZigbeeKnownDevices* devices) noexcept {
    if (data == nullptr || devices == nullptr || size < 8 ||
        size > kZigbeeKnownDevicesBlobCapacity || data[0] != 'Z' ||
        data[1] != 'K' || data[2] != kZigbeeKnownDevicesSchemaVersion ||
        data[3] > kMaxPersistedZigbeeShortAddresses ||
        size != 8 + static_cast<std::size_t>(data[3]) * 2) {
        return false;
    }
    const std::size_t payload_size = size - 4;
    std::uint32_t stored_crc = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        stored_crc |= static_cast<std::uint32_t>(data[payload_size + index])
            << (index * 8U);
    }
    if (stored_crc != zigbee_known_devices_crc32(data, payload_size)) {
        return false;
    }
    ZigbeeKnownDevices decoded{};
    for (std::size_t index = 0; index < data[3]; ++index) {
        const std::size_t offset = 4 + index * 2;
        const std::uint16_t address = static_cast<std::uint16_t>(data[offset]) |
            static_cast<std::uint16_t>(data[offset + 1] << 8U);
        if (decoded.contains(address) || !decoded.add(address)) {
            return false;
        }
    }
    *devices = decoded;
    return true;
}

}