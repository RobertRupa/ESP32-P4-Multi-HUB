#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace hub::diagnostics {

class WebSecurity final {
public:
    esp_err_t start() noexcept;
    bool verify_password(const char* username, std::size_t username_length,
                        const char* password, std::size_t password_length) noexcept;
    esp_err_t set_password(const char* password,
                           std::size_t password_length) noexcept;
    esp_err_t reset_password(char* one_time_password,
                             std::size_t capacity) noexcept;
    esp_err_t create_session(char* token, std::size_t capacity) noexcept;
    bool verify_session(const char* token, std::size_t length) noexcept;
    void revoke_session() noexcept;
    bool password_change_required() const noexcept;
private:
    static constexpr std::size_t kSaltSize = 16;
    static constexpr std::size_t kPasswordHashSize = 32;
    static constexpr std::size_t kSessionTokenLength = 32;
#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
    static constexpr std::uint32_t kPasswordIterations = 10000;
#else
    static constexpr std::uint32_t kPasswordIterations = 100000;
#endif
    static constexpr std::int64_t kSessionLifetimeUs = 600000000;

    esp_err_t initialize_password_store() noexcept;
    esp_err_t derive_password_hash(const char* password,
                                   std::size_t password_length,
                                   const std::uint8_t* salt,
                                   std::uint8_t* output) noexcept;

    std::array<std::uint8_t, kSaltSize> salt_{};
    std::array<std::uint8_t, kPasswordHashSize> password_hash_{};
    std::array<char, kSessionTokenLength + 1> session_token_{};
    bool password_change_required_ = true;
    std::uint8_t failed_attempts_ = 0;
    std::int64_t lockout_until_us_ = 0;
    std::int64_t session_expires_at_us_ = 0;
};

}
