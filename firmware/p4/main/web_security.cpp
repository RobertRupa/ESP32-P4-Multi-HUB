#include "web_security.hpp"

#include <cstring>
#include <cstdio>

#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "psa/crypto.h"

namespace hub::diagnostics {
namespace {

constexpr char kAuthNamespace[] = "web_auth";
constexpr char kPasswordSaltKey[] = "pw_salt";
constexpr char kPasswordHashKey[] = "pw_hash";
constexpr char kPasswordChangeKey[] = "pw_change";
constexpr char kPasswordResetCountKey[] = "pw_reset_count";
constexpr char kPasswordResetTimeKey[] = "pw_reset_time";
constexpr char kDevelopmentModeKey[] = "dev_web_mode";
constexpr char kDevelopmentMigrationCountKey[] = "dev_mig_count";
constexpr char kDevelopmentMigrationTimeKey[] = "dev_mig_time";
constexpr char kInitialUsername[] = "admin";
#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
constexpr char kDevelopmentPassword[] = "admin";
#endif
constexpr std::size_t kMaxPasswordLength = 64;
constexpr std::int64_t kLockoutDurationUs = 60000000;
constexpr char kTag[] = "web_security";
constexpr char kHexDigits[] = "0123456789abcdef";

}

esp_err_t WebSecurity::start() noexcept {
    if (!esp_flash_encryption_enabled()) {
        return ESP_ERR_INVALID_STATE;
    }
    const psa_status_t crypto_status = psa_crypto_init();
    if (crypto_status != PSA_SUCCESS) {
        ESP_LOGE(kTag, "PSA initialization failed: %ld",
                 static_cast<long>(crypto_status));
        return ESP_FAIL;
    }
    const esp_err_t status = initialize_password_store();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "Password store initialization failed: %s",
                 esp_err_to_name(status));
    }
    return status;
}

bool WebSecurity::verify_password(const char* username,
                                  std::size_t username_length,
                                  const char* password,
                                  std::size_t password_length) noexcept {
    if (username == nullptr || password == nullptr ||
        username_length != sizeof(kInitialUsername) - 1 ||
        std::memcmp(username, kInitialUsername, username_length) != 0 ||
        password_length == 0 || password_length > kMaxPasswordLength) {
        return false;
    }

    const std::int64_t now_us = esp_timer_get_time();
    if (lockout_until_us_ > now_us) {
        return false;
    }

    std::array<std::uint8_t, kPasswordHashSize> candidate{};
    if (derive_password_hash(password, password_length, salt_.data(),
                             candidate.data()) != ESP_OK) {
        return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        difference |= candidate[index] ^ password_hash_[index];
    }
    if (difference == 0) {
        failed_attempts_ = 0;
        lockout_until_us_ = 0;
        return true;
    }

    if (++failed_attempts_ >= 5) {
        failed_attempts_ = 0;
        lockout_until_us_ = now_us + kLockoutDurationUs;
    }
    return false;
}

esp_err_t WebSecurity::set_password(const char* password,
                                    std::size_t password_length) noexcept {
    if (password == nullptr || password_length < 12 ||
        password_length > kMaxPasswordLength) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (std::size_t index = 0; index < password_length; ++index) {
        const unsigned char value = static_cast<unsigned char>(password[index]);
        if (value < 0x20U || value == 0x7FU) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    std::array<std::uint8_t, kSaltSize> new_salt{};
    std::array<std::uint8_t, kPasswordHashSize> new_hash{};
    if (psa_generate_random(new_salt.data(), new_salt.size()) != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    esp_err_t status = derive_password_hash(password, password_length,
                                            new_salt.data(), new_hash.data());
    if (status != ESP_OK) {
        return status;
    }

    nvs_handle_t handle = 0;
    status = nvs_open(kAuthNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_blob(handle, kPasswordSaltKey, new_salt.data(), new_salt.size());
    if (status == ESP_OK) {
        status = nvs_set_blob(handle, kPasswordHashKey, new_hash.data(), new_hash.size());
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, kPasswordChangeKey, 0);
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    if (status == ESP_OK) {
        salt_ = new_salt;
        password_hash_ = new_hash;
        password_change_required_ = false;
        failed_attempts_ = 0;
        lockout_until_us_ = 0;
        revoke_session();
    }
    return status;
}

esp_err_t WebSecurity::reset_password(char* one_time_password,
                                      std::size_t capacity) noexcept {
    constexpr std::size_t kBootstrapRandomSize = 16;
    constexpr std::size_t kBootstrapPasswordLength = kBootstrapRandomSize * 2;
    if (one_time_password == nullptr || capacity < kBootstrapPasswordLength + 1) {
        return ESP_ERR_INVALID_ARG;
    }

    std::array<std::uint8_t, kBootstrapRandomSize> bootstrap_random{};
    std::array<std::uint8_t, kSaltSize> new_salt{};
    std::array<std::uint8_t, kPasswordHashSize> new_hash{};
    std::array<char, kBootstrapPasswordLength + 1> new_password{};
    if (psa_generate_random(bootstrap_random.data(), bootstrap_random.size()) != PSA_SUCCESS ||
        psa_generate_random(new_salt.data(), new_salt.size()) != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    for (std::size_t index = 0; index < bootstrap_random.size(); ++index) {
        new_password[index * 2] = kHexDigits[bootstrap_random[index] >> 4U];
        new_password[index * 2 + 1] = kHexDigits[bootstrap_random[index] & 0x0FU];
    }
    esp_err_t status = derive_password_hash(
        new_password.data(), kBootstrapPasswordLength,
        new_salt.data(), new_hash.data());
    if (status != ESP_OK) {
        return status;
    }

    nvs_handle_t handle = 0;
    status = nvs_open(kAuthNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    std::uint32_t reset_count = 0;
    status = nvs_get_u32(handle, kPasswordResetCountKey, &reset_count);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        status = ESP_OK;
    }
    if (status == ESP_OK && reset_count == UINT32_MAX) {
        status = ESP_ERR_INVALID_STATE;
    }
    if (status == ESP_OK) {
        status = nvs_set_blob(handle, kPasswordSaltKey, new_salt.data(), new_salt.size());
    }
    if (status == ESP_OK) {
        status = nvs_set_blob(handle, kPasswordHashKey, new_hash.data(), new_hash.size());
    }
    if (status == ESP_OK) {
        status = nvs_set_u8(handle, kPasswordChangeKey, 1);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, kPasswordResetCountKey, reset_count + 1U);
    }
    if (status == ESP_OK) {
        status = nvs_set_u32(handle, kPasswordResetTimeKey,
                             static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
    }
#if !CONFIG_HUB_WEB_DEVELOPMENT_MODE
    if (status == ESP_OK) {
        const esp_err_t dev_mode_status = nvs_erase_key(handle, kDevelopmentModeKey);
        if (dev_mode_status != ESP_OK && dev_mode_status != ESP_ERR_NVS_NOT_FOUND) {
            status = dev_mode_status;
        }
    }
#endif
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);

    if (status == ESP_OK) {
        salt_ = new_salt;
        password_hash_ = new_hash;
        password_change_required_ = true;
        failed_attempts_ = 0;
        lockout_until_us_ = 0;
        revoke_session();
        std::memcpy(one_time_password, new_password.data(), new_password.size());
    }
    std::memset(bootstrap_random.data(), 0, bootstrap_random.size());
    std::memset(new_password.data(), 0, new_password.size());
    std::memset(new_hash.data(), 0, new_hash.size());
    return status;
}

esp_err_t WebSecurity::create_session(char* token, std::size_t capacity) noexcept {
    if (token == nullptr || capacity < session_token_.size()) {
        return ESP_ERR_INVALID_ARG;
    }
    std::array<std::uint8_t, kSessionTokenLength / 2> random{};
    if (psa_generate_random(random.data(), random.size()) != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    for (std::size_t index = 0; index < random.size(); ++index) {
        session_token_[index * 2] = kHexDigits[random[index] >> 4U];
        session_token_[index * 2 + 1] = kHexDigits[random[index] & 0x0FU];
    }
    session_token_[kSessionTokenLength] = '\0';
    session_expires_at_us_ = esp_timer_get_time() + kSessionLifetimeUs;
    std::memcpy(token, session_token_.data(), session_token_.size());
    std::memset(random.data(), 0, random.size());
    return ESP_OK;
}

bool WebSecurity::verify_session(const char* token, std::size_t length) noexcept {
    if (token == nullptr || length != kSessionTokenLength ||
        session_expires_at_us_ <= esp_timer_get_time()) {
        revoke_session();
        return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < kSessionTokenLength; ++index) {
        difference |= static_cast<std::uint8_t>(token[index] ^ session_token_[index]);
    }
    return difference == 0;
}

void WebSecurity::revoke_session() noexcept {
    std::memset(session_token_.data(), 0, session_token_.size());
    session_expires_at_us_ = 0;
}

bool WebSecurity::password_change_required() const noexcept {
    return password_change_required_;
}

esp_err_t WebSecurity::initialize_password_store() noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open(kAuthNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }

    std::size_t salt_length = salt_.size();
    std::size_t hash_length = password_hash_.size();
    status = nvs_get_blob(handle, kPasswordSaltKey, salt_.data(), &salt_length);
    const esp_err_t salt_status = status;
    status = nvs_get_blob(handle, kPasswordHashKey, password_hash_.data(), &hash_length);
    const esp_err_t hash_status = status;
    std::uint8_t change_required = 0;
    status = nvs_get_u8(handle, kPasswordChangeKey, &change_required);
    const esp_err_t change_status = status;

#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
    std::uint8_t development_mode_initialized = 0;
    const esp_err_t development_mode_status = nvs_get_u8(
        handle, kDevelopmentModeKey, &development_mode_initialized);
    if (development_mode_status == ESP_ERR_NVS_NOT_FOUND) {
        std::array<std::uint8_t, kSaltSize> development_salt{};
        std::array<std::uint8_t, kPasswordHashSize> development_hash{};
        if (psa_generate_random(development_salt.data(), development_salt.size()) !=
            PSA_SUCCESS) {
            nvs_close(handle);
            return ESP_FAIL;
        }
        status = derive_password_hash(
            kDevelopmentPassword, sizeof(kDevelopmentPassword) - 1,
            development_salt.data(), development_hash.data());
        std::uint32_t migration_count = 0;
        if (status == ESP_OK) {
            const esp_err_t count_status = nvs_get_u32(
                handle, kDevelopmentMigrationCountKey, &migration_count);
            if (count_status != ESP_OK && count_status != ESP_ERR_NVS_NOT_FOUND) {
                status = count_status;
            }
        }
        if (status == ESP_OK && migration_count == UINT32_MAX) {
            status = ESP_ERR_INVALID_STATE;
        }
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kPasswordSaltKey,
                                  development_salt.data(), development_salt.size());
        }
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kPasswordHashKey,
                                  development_hash.data(), development_hash.size());
        }
        if (status == ESP_OK) {
            status = nvs_set_u8(handle, kPasswordChangeKey, 0);
        }
        if (status == ESP_OK) {
            status = nvs_set_u8(handle, kDevelopmentModeKey, 1);
        }
        if (status == ESP_OK) {
            status = nvs_set_u32(handle, kDevelopmentMigrationCountKey,
                                 migration_count + 1U);
        }
        if (status == ESP_OK) {
            status = nvs_set_u32(handle, kDevelopmentMigrationTimeKey,
                static_cast<std::uint32_t>(esp_timer_get_time() / 1000000));
        }
        if (status == ESP_OK) {
            status = nvs_commit(handle);
        }
        nvs_close(handle);
        if (status == ESP_OK) {
            salt_ = development_salt;
            password_hash_ = development_hash;
            password_change_required_ = false;
            failed_attempts_ = 0;
            lockout_until_us_ = 0;
            ESP_LOGW(kTag, "Development Web UI credential provisioned");
        } else {
            ESP_LOGE(kTag, "Development Web UI credential setup failed: %s",
                     esp_err_to_name(status));
        }
        std::memset(development_hash.data(), 0, development_hash.size());
        return status;
    }
    if (development_mode_status != ESP_OK || development_mode_initialized != 1) {
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }
#else
    std::uint8_t development_mode_initialized = 0;
    const esp_err_t development_mode_status = nvs_get_u8(
        handle, kDevelopmentModeKey, &development_mode_initialized);
    if (development_mode_status == ESP_OK && development_mode_initialized == 1) {
        nvs_close(handle);
        ESP_LOGE(kTag, "Developer Web UI NVS cannot be used in production mode");
        return ESP_ERR_INVALID_STATE;
    }
    if (development_mode_status != ESP_OK &&
        development_mode_status != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return development_mode_status;
    }
#endif

    const bool empty = salt_status == ESP_ERR_NVS_NOT_FOUND &&
        hash_status == ESP_ERR_NVS_NOT_FOUND &&
        change_status == ESP_ERR_NVS_NOT_FOUND;
    if (empty) {
        std::array<std::uint8_t, 16> bootstrap_random{};
        std::array<char, bootstrap_random.size() * 2 + 1> bootstrap_password{};
        if (psa_generate_random(salt_.data(), salt_.size()) != PSA_SUCCESS) {
            ESP_LOGE(kTag, "Bootstrap salt generation failed");
            nvs_close(handle);
            return ESP_FAIL;
        }
        if (psa_generate_random(bootstrap_random.data(), bootstrap_random.size()) !=
            PSA_SUCCESS) {
            ESP_LOGE(kTag, "Bootstrap password generation failed");
            nvs_close(handle);
            return ESP_FAIL;
        }
        for (std::size_t index = 0; index < bootstrap_random.size(); ++index) {
            bootstrap_password[index * 2] = kHexDigits[bootstrap_random[index] >> 4U];
            bootstrap_password[index * 2 + 1] = kHexDigits[bootstrap_random[index] & 0x0FU];
        }
        status = derive_password_hash(bootstrap_password.data(),
                                      bootstrap_password.size() - 1,
                                      salt_.data(), password_hash_.data());
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kPasswordSaltKey, salt_.data(), salt_.size());
        }
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kPasswordHashKey,
                                  password_hash_.data(), password_hash_.size());
        }
        if (status == ESP_OK) {
            status = nvs_set_u8(handle, kPasswordChangeKey, 1);
        }
        if (status == ESP_OK) {
            status = nvs_commit(handle);
        }
        if (status == ESP_OK) {
            ESP_LOGW(kTag, "One-time Web UI password for admin (change immediately): %s",
                     bootstrap_password.data());
        } else {
            ESP_LOGE(kTag, "Bootstrap credential persistence failed: %s",
                     esp_err_to_name(status));
        }
        std::memset(bootstrap_password.data(), 0, bootstrap_password.size());
        std::memset(bootstrap_random.data(), 0, bootstrap_random.size());
        password_change_required_ = true;
    } else if (salt_status == ESP_OK && hash_status == ESP_OK &&
               change_status == ESP_OK && salt_length == salt_.size() &&
               hash_length == password_hash_.size() && change_required <= 1) {
        password_change_required_ = change_required != 0;
    } else {
        status = ESP_ERR_INVALID_STATE;
    }
    nvs_close(handle);
    return status;
}

esp_err_t WebSecurity::derive_password_hash(const char* password,
                                            std::size_t password_length,
                                            const std::uint8_t* salt,
                                            std::uint8_t* output) noexcept {
    if (password == nullptr || salt == nullptr || output == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const int status = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        reinterpret_cast<const unsigned char*>(password), password_length,
        reinterpret_cast<const unsigned char*>(salt), kSaltSize,
        kPasswordIterations, static_cast<std::uint32_t>(kPasswordHashSize),
        reinterpret_cast<unsigned char*>(output));
    if (status != 0) {
        ESP_LOGE(kTag, "PBKDF2 derivation failed: -0x%04x",
                 static_cast<unsigned>(-status));
    }
    return status == 0 ? ESP_OK : ESP_FAIL;
}

}
