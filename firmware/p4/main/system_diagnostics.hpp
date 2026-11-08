#pragma once

#include <cstdint>

#include "esp_flash_encrypt.h"
#include "esp_heap_caps.h"

namespace hub::diagnostics {

struct SystemSnapshot {
    bool flash_encryption_enabled = false;
    std::uint32_t psram_free_kb = 0;
    std::uint32_t internal_heap_free_kb = 0;
};

inline SystemSnapshot read_system_snapshot() noexcept {
    return {
        esp_flash_encryption_enabled(),
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U),
        static_cast<std::uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U),
    };
}

}