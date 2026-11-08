#include "web_security.hpp"

#include <cstring>
#include <cstdio>

#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/pk.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"

namespace hub::diagnostics {
namespace {

constexpr char kAuthNamespace[] = "web_auth";
constexpr char kTlsNamespace[] = "web_tls";
constexpr char kPasswordSaltKey[] = "pw_salt";
constexpr char kPasswordHashKey[] = "pw_hash";
constexpr char kPasswordChangeKey[] = "pw_change";
constexpr char kPasswordResetCountKey[] = "pw_reset_count";
constexpr char kPasswordResetTimeKey[] = "pw_reset_time";
constexpr char kDevelopmentModeKey[] = "dev_web_mode";
constexpr char kDevelopmentMigrationCountKey[] = "dev_mig_count";
constexpr char kDevelopmentMigrationTimeKey[] = "dev_mig_time";
constexpr char kCertificateKey[] = "cert";
constexpr char kPrivateKeyKey[] = "key";
constexpr char kInitialUsername[] = "admin";
#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
constexpr char kDevelopmentPassword[] = "admin";
#endif
constexpr std::size_t kMaxPasswordLength = 64;
constexpr std::int64_t kLockoutDurationUs = 60000000;
constexpr char kTag[] = "web_security";
constexpr char kHexDigits[] = "0123456789abcdef";

int psa_random_callback(void* context, unsigned char* output,
                        std::size_t length) noexcept {
    (void)context;
    return psa_generate_random(output, length) == PSA_SUCCESS ? 0 : -1;
}

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
    esp_err_t status = initialize_tls_identity();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "TLS identity initialization failed: %s",
                 esp_err_to_name(status));
    }
    if (status == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
        status = initialize_password_store();
        if (status != ESP_OK) {
            ESP_LOGE(kTag, "Password store initialization failed: %s",
                     esp_err_to_name(status));
        }
    }
    if (status == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
        status = validate_and_log_tls_identity();
        if (status != ESP_OK) {
            ESP_LOGE(kTag, "TLS identity validation failed: %s",
                     esp_err_to_name(status));
        }
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

const std::uint8_t* WebSecurity::certificate(std::size_t* length) const noexcept {
    if (length == nullptr || certificate_length_ == 0) {
        return nullptr;
    }
    *length = certificate_length_;
    return certificate_.data();
}

const std::uint8_t* WebSecurity::private_key(std::size_t* length) const noexcept {
    if (length == nullptr || private_key_length_ == 0) {
        return nullptr;
    }
    *length = private_key_length_;
    return private_key_.data();
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

esp_err_t WebSecurity::initialize_tls_identity() noexcept {
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open(kTlsNamespace, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }

    std::size_t certificate_length = certificate_.size();
    std::size_t private_key_length = private_key_.size();
    const esp_err_t certificate_status = nvs_get_blob(
        handle, kCertificateKey, certificate_.data(), &certificate_length);
    const esp_err_t private_key_status = nvs_get_blob(
        handle, kPrivateKeyKey, private_key_.data(), &private_key_length);
    if (certificate_status == ESP_ERR_NVS_NOT_FOUND &&
        private_key_status == ESP_ERR_NVS_NOT_FOUND) {
        status = generate_tls_identity();
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kCertificateKey, certificate_.data(),
                                  certificate_length_);
        }
        if (status == ESP_OK) {
            status = nvs_set_blob(handle, kPrivateKeyKey, private_key_.data(),
                                  private_key_length_);
        }
        if (status == ESP_OK) {
            status = nvs_commit(handle);
        }
    } else if (certificate_status == ESP_OK && private_key_status == ESP_OK &&
               certificate_length > 1 && certificate_length <= certificate_.size() &&
               private_key_length > 1 && private_key_length <= private_key_.size()) {
        certificate_length_ = certificate_length;
        private_key_length_ = private_key_length;
        if (certificate_.back() != 0 || private_key_.back() != 0) {
            status = ESP_ERR_INVALID_STATE;
        }
    } else {
        status = ESP_ERR_INVALID_STATE;
    }
    nvs_close(handle);
    return status;
}

esp_err_t WebSecurity::validate_and_log_tls_identity() noexcept {
    if (certificate_length_ < 2 || private_key_length_ < 2) {
        return ESP_ERR_INVALID_STATE;
    }

    mbedtls_x509_crt parsed_certificate;
    mbedtls_pk_context parsed_private_key;
    mbedtls_x509_crt_init(&parsed_certificate);
    mbedtls_pk_init(&parsed_private_key);
    int status = mbedtls_x509_crt_parse(&parsed_certificate,
                                        certificate_.data(),
                                        certificate_length_);
    if (status == 0) {
        status = mbedtls_pk_parse_key(&parsed_private_key, private_key_.data(),
                                      private_key_length_, nullptr, 0,
                                      psa_random_callback, nullptr);
    }
    if (status == 0) {
        status = mbedtls_pk_check_pair(&parsed_certificate.pk,
                                       &parsed_private_key,
                                       psa_random_callback, nullptr);
    }

    std::array<std::uint8_t, 32> fingerprint{};
    std::size_t fingerprint_length = 0;
    psa_status_t hash_status = PSA_ERROR_INVALID_ARGUMENT;
    if (status == 0) {
        hash_status = psa_hash_compute(
            PSA_ALG_SHA_256, parsed_certificate.raw.p,
            parsed_certificate.raw.len, fingerprint.data(),
            fingerprint.size(), &fingerprint_length);
    }
    mbedtls_pk_free(&parsed_private_key);
    mbedtls_x509_crt_free(&parsed_certificate);
    if (status != 0 || hash_status != PSA_SUCCESS ||
        fingerprint_length != fingerprint.size()) {
        return ESP_FAIL;
    }

    char fingerprint_text[96]{};
    std::size_t output_index = 0;
    for (std::size_t index = 0; index < fingerprint.size(); ++index) {
        const int written = std::snprintf(
            fingerprint_text + output_index,
            sizeof(fingerprint_text) - output_index,
            index == 0 ? "%02x" : ":%02x",
            static_cast<unsigned>(fingerprint[index]));
        if (written <= 0 || static_cast<std::size_t>(written) >=
            sizeof(fingerprint_text) - output_index) {
            return ESP_FAIL;
        }
        output_index += static_cast<std::size_t>(written);
    }
    ESP_LOGI(kTag, "HTTPS certificate SHA-256 fingerprint: %s", fingerprint_text);
    return ESP_OK;
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

esp_err_t WebSecurity::generate_tls_identity() noexcept {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT |
                                         PSA_KEY_USAGE_SIGN_HASH |
                                         PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key_id = 0;
    psa_status_t psa_result = psa_generate_key(&attributes, &key_id);
    psa_reset_key_attributes(&attributes);
    if (psa_result != PSA_SUCCESS) {
        ESP_LOGE(kTag, "TLS key generation failed: %ld",
                 static_cast<long>(psa_result));
        return ESP_FAIL;
    }

    std::array<std::uint8_t, 64> private_material{};
    std::array<std::uint8_t, 128> public_material{};
    std::size_t private_length = 0;
    std::size_t public_length = 0;
    psa_result = psa_export_key(key_id, private_material.data(),
                                private_material.size(), &private_length);
    if (psa_result == PSA_SUCCESS) {
        psa_result = psa_export_public_key(key_id, public_material.data(),
                                           public_material.size(), &public_length);
    }
    (void)psa_destroy_key(key_id);
    if (psa_result != PSA_SUCCESS || private_length != 32 || public_length != 65) {
        ESP_LOGE(kTag, "TLS key export failed: status=%ld private=%u public=%u",
                 static_cast<long>(psa_result),
                 static_cast<unsigned>(private_length),
                 static_cast<unsigned>(public_length));
        return ESP_FAIL;
    }

    std::array<unsigned char, 128> private_der{};
    const std::uint8_t der_prefix[] = {
        0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20,
    };
    const std::uint8_t curve_oid[] = {
        0xA0, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE,
        0x3D, 0x03, 0x01, 0x07,
    };
    const std::uint8_t public_prefix[] = {
        0xA1, 0x44, 0x03, 0x42, 0x00,
    };
    std::size_t der_length = 0;
    std::memcpy(private_der.data() + der_length, der_prefix, sizeof(der_prefix));
    der_length += sizeof(der_prefix);
    std::memcpy(private_der.data() + der_length, private_material.data(), private_length);
    der_length += private_length;
    std::memcpy(private_der.data() + der_length, curve_oid, sizeof(curve_oid));
    der_length += sizeof(curve_oid);
    std::memcpy(private_der.data() + der_length, public_prefix, sizeof(public_prefix));
    der_length += sizeof(public_prefix);
    std::memcpy(private_der.data() + der_length, public_material.data(), public_length);
    der_length += public_length;

    mbedtls_pk_context private_key;
    mbedtls_pk_init(&private_key);
    int tls_result = mbedtls_pk_parse_key(&private_key, private_der.data(),
                                          der_length, nullptr, 0,
                                          psa_random_callback, nullptr);
    if (tls_result != 0) {
        ESP_LOGE(kTag, "TLS private-key parse failed: -0x%04x",
                 static_cast<unsigned>(-tls_result));
    }
    if (tls_result == 0) {
        tls_result = mbedtls_pk_write_key_pem(&private_key, private_key_.data(),
                                               private_key_.size());
        if (tls_result != 0) {
            ESP_LOGE(kTag, "TLS private-key PEM export failed: -0x%04x",
                     static_cast<unsigned>(-tls_result));
        }
    }

    mbedtls_x509write_cert certificate_writer;
    mbedtls_x509write_crt_init(&certificate_writer);
    if (tls_result == 0) {
        mbedtls_x509write_crt_set_version(&certificate_writer,
                                          MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&certificate_writer, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&certificate_writer, &private_key);
        mbedtls_x509write_crt_set_issuer_key(&certificate_writer, &private_key);
        tls_result = mbedtls_x509write_crt_set_subject_name(
            &certificate_writer, "CN=P4 Multi Hub");
    }
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_set_issuer_name(
            &certificate_writer, "CN=P4 Multi Hub");
    }
    std::array<std::uint8_t, 8> serial{};
    if (tls_result == 0 && psa_generate_random(serial.data(), serial.size()) != PSA_SUCCESS) {
        tls_result = -1;
    }
    serial[0] &= 0x7FU;
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_set_serial_raw(
            &certificate_writer, serial.data(), serial.size());
    }
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_set_validity(
            &certificate_writer, "20200101000000", "20491231235959");
    }
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_set_basic_constraints(
            &certificate_writer, 0, -1);
    }
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_set_key_usage(
            &certificate_writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    }
    if (tls_result == 0) {
        tls_result = mbedtls_x509write_crt_pem(
            &certificate_writer, certificate_.data(), certificate_.size(),
            psa_random_callback, nullptr);
    }
    mbedtls_x509write_crt_free(&certificate_writer);
    mbedtls_pk_free(&private_key);
    std::memset(private_material.data(), 0, private_material.size());
    std::memset(private_der.data(), 0, private_der.size());
    if (tls_result != 0) {
        ESP_LOGE(kTag, "TLS certificate generation failed: -0x%04x",
                 static_cast<unsigned>(-tls_result));
        return ESP_FAIL;
    }

    certificate_length_ = std::strlen(
        reinterpret_cast<const char*>(certificate_.data())) + 1;
    private_key_length_ = std::strlen(
        reinterpret_cast<const char*>(private_key_.data())) + 1;
    return ESP_OK;
}

}
