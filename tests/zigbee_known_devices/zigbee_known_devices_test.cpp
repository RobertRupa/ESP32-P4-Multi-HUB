#include "zigbee_known_devices.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using hub::radio::ZigbeeKnownDevices;
    using hub::radio::kZigbeeKnownDevicesBlobCapacity;
    using hub::radio::decode_zigbee_known_devices;
    using hub::radio::encode_zigbee_known_devices;

    ZigbeeKnownDevices devices{};
    assert(devices.add(0x8362));
    assert(devices.add(0x2C0C));
    assert(devices.add(0x8362));
    assert(devices.count == 2);
    assert(!devices.add(0));
    assert(!devices.add(0xFFFF));

    std::array<std::uint8_t, kZigbeeKnownDevicesBlobCapacity> blob{};
    std::size_t blob_size = 0;
    assert(encode_zigbee_known_devices(devices, &blob, &blob_size));
    assert(blob_size == 12);

    ZigbeeKnownDevices restored{};
    assert(decode_zigbee_known_devices(blob.data(), blob_size, &restored));
    assert(restored.count == 2);
    assert(restored.addresses[0] == 0x8362);
    assert(restored.addresses[1] == 0x2C0C);

    auto corrupted = blob;
    corrupted[5] ^= 0x80;
    assert(!decode_zigbee_known_devices(corrupted.data(), blob_size, &restored));
    assert(!decode_zigbee_known_devices(blob.data(), blob_size - 1, &restored));

    auto duplicate = blob;
    duplicate[6] = duplicate[4];
    duplicate[7] = duplicate[5];
    const std::uint32_t duplicate_crc =
        hub::radio::zigbee_known_devices_crc32(duplicate.data(), 8);
    for (std::size_t index = 0; index < 4; ++index) {
        duplicate[8 + index] = static_cast<std::uint8_t>(
            duplicate_crc >> (index * 8U));
    }
    assert(!decode_zigbee_known_devices(duplicate.data(), 12, &restored));

    ZigbeeKnownDevices full{};
    for (std::uint16_t index = 1; index <= full.addresses.size(); ++index) {
        assert(full.add(index));
    }
    assert(!full.add(0x1000));
    assert(encode_zigbee_known_devices(full, &blob, &blob_size));
    assert(blob_size == kZigbeeKnownDevicesBlobCapacity);
    assert(decode_zigbee_known_devices(blob.data(), blob_size, &restored));
    assert(restored.count == full.addresses.size());

    std::array<std::uint8_t, kZigbeeKnownDevicesBlobCapacity> unsupported{};
    unsupported[0] = 'Z';
    unsupported[1] = 'K';
    unsupported[2] = 2;
    assert(!decode_zigbee_known_devices(unsupported.data(), 8, &restored));
    return 0;
}