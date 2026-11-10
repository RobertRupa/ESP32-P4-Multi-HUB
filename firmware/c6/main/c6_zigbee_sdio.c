#include "c6_zigbee_sdio.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "eh_cp_feat_peer_data.h"
#include "esp_coexist.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "c6_zigbee_sdio";

#define C6_ZB_STATUS_MSG_ID 0x5A420101U
#define C6_ZB_COMMAND_MSG_ID 0x5A420102U
#define C6_ZB_STATUS_VERSION 3U
#define C6_ZB_STATUS_VERSION_2 2U
#define C6_ZB_COMMAND_VERSION 1U
#define C6_ZB_COMMAND_STATUS 0U
#define C6_ZB_COMMAND_SEARCH 1U
#define C6_ZB_COMMAND_CREATE_NETWORK 2U
#define C6_ZB_COMMAND_OPEN_JOIN_WINDOW 3U
#define C6_ZB_STATUS_SIZE 17U
#define C6_ZB_COMMAND_SIZE 3U
#define C6_ZB_MAX_TRACKED_DEVICES 64U
#define C6_ZB_JOIN_WINDOW_SECONDS 180U
#define C6_ZB_COMMISSIONING_TIMEOUT_SECONDS 120U

typedef enum {
    C6_ZB_BDB_STAGE_NONE = 0,
    C6_ZB_BDB_STAGE_INITIALIZATION = 1,
    C6_ZB_BDB_STAGE_FORMATION_START = 2,
    C6_ZB_BDB_STAGE_FORMATION_SIGNAL = 3,
    C6_ZB_BDB_STAGE_CLOSE_NETWORK = 4,
    C6_ZB_BDB_STAGE_OPEN_JOIN_WINDOW = 5,
    C6_ZB_BDB_STAGE_TIMEOUT = 6,
} c6_zb_bdb_stage_t;

typedef struct {
    uint8_t payload[C6_ZB_STATUS_SIZE];
} c6_zb_status_message_t;

typedef struct {
    uint8_t command;
    uint8_t duration_seconds;
} c6_zb_command_t;

static QueueHandle_t s_status_queue;
static QueueHandle_t s_command_queue;
static TaskHandle_t s_zigbee_task;
static TaskHandle_t s_peer_data_task;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_stack_ready;
static bool s_bdb_ready;
static bool s_network_formed;
static bool s_search_pending;
static bool s_formation_timed_out;
static bool s_join_after_formation;
static uint8_t s_join_seconds;
static uint8_t s_requested_join_seconds;
static uint16_t s_device_count;
static uint16_t s_last_signal_type;
static uint8_t s_last_bdb_status;
static int32_t s_last_error;
static int32_t s_last_bdb_result;
static uint8_t s_last_bdb_stage;
static int64_t s_commissioning_started_us;
static uint64_t s_device_ids[C6_ZB_MAX_TRACKED_DEVICES];

static void publish_status(void)
{
    c6_zb_status_message_t message = {0};
    portENTER_CRITICAL(&s_state_lock);
    message.payload[0] = C6_ZB_STATUS_VERSION;
    message.payload[1] = (s_stack_ready && s_bdb_ready ? 0x01U : 0U) |
                        (s_network_formed ? 0x02U : 0U) |
                        (s_search_pending ? 0x04U : 0U) |
                        (s_formation_timed_out ? 0x08U : 0U);
    message.payload[2] = (uint8_t)(s_device_count & 0xFFU);
    message.payload[3] = (uint8_t)(s_device_count >> 8U);
    message.payload[4] = s_join_seconds;
    message.payload[5] = (uint8_t)(s_last_error & 0xFF);
    message.payload[6] = (uint8_t)((s_last_error >> 8) & 0xFF);
    message.payload[7] = (uint8_t)((s_last_error >> 16) & 0xFF);
    message.payload[8] = (uint8_t)((s_last_error >> 24) & 0xFF);
    message.payload[9] = (uint8_t)(s_last_signal_type & 0xFFU);
    message.payload[10] = (uint8_t)(s_last_signal_type >> 8U);
    message.payload[11] = s_last_bdb_status;
    message.payload[12] = (uint8_t)(s_last_bdb_result & 0xFF);
    message.payload[13] = (uint8_t)((s_last_bdb_result >> 8) & 0xFF);
    message.payload[14] = (uint8_t)((s_last_bdb_result >> 16) & 0xFF);
    message.payload[15] = (uint8_t)((s_last_bdb_result >> 24) & 0xFF);
    message.payload[16] = s_last_bdb_stage;
    portEXIT_CRITICAL(&s_state_lock);
    if (s_status_queue != NULL && xQueueSend(s_status_queue, &message, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Status queue full; dropping coalescible snapshot");
    }
}

static esp_err_t persist_audit(const char *count_key, const char *time_key)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open("c6_zb_audit", NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint32_t attempts = 0;
    result = nvs_get_u32(handle, count_key, &attempts);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = nvs_set_u32(handle, count_key, attempts + 1U);
    }
    if (result == ESP_OK) {
        result = nvs_set_u32(handle, time_key,
                             (uint32_t)(esp_timer_get_time() / 1000000));
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static esp_err_t persist_search_audit(void)
{
    return persist_audit("search_count", "search_uptime_s");
}

static esp_err_t persist_network_audit(void)
{
    return persist_audit("network_count", "network_time_s");
}

static void expire_commissioning_request(void)
{
    const int64_t now_us = esp_timer_get_time();
    bool expired = false;
    uint8_t commissioning_status = 0xFFU;
    uint16_t last_signal_type = 0;
    uint8_t last_bdb_status = 0;
    portENTER_CRITICAL(&s_state_lock);
    if (s_search_pending && s_commissioning_started_us > 0 &&
        now_us - s_commissioning_started_us >=
            (int64_t)C6_ZB_COMMISSIONING_TIMEOUT_SECONDS * 1000000LL) {
        s_search_pending = false;
        s_join_after_formation = false;
        s_join_seconds = 0;
        s_last_bdb_stage = C6_ZB_BDB_STAGE_TIMEOUT;
        s_formation_timed_out = true;
        s_last_error = ESP_ERR_TIMEOUT;
        s_commissioning_started_us = 0;
        last_signal_type = s_last_signal_type;
        last_bdb_status = s_last_bdb_status;
        expired = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (expired) {
        if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
            commissioning_status = ezb_bdb_get_commissioning_status();
            esp_zigbee_lock_release();
        }
        portENTER_CRITICAL(&s_state_lock);
        if (commissioning_status != 0xFFU) {
            s_last_bdb_status = commissioning_status;
        }
        last_signal_type = s_last_signal_type;
        last_bdb_status = s_last_bdb_status;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGE(TAG, "BDB timeout: elapsed_s=%u watchdog_error=%s last_signal=0x%04x last_bdb_status=%u queried_status=%u",
                 (unsigned)C6_ZB_COMMISSIONING_TIMEOUT_SECONDS,
                 esp_err_to_name(ESP_ERR_TIMEOUT),
                 (unsigned)last_signal_type, (unsigned)last_bdb_status,
                 (unsigned)commissioning_status);
        publish_status();
    }
}

static void peer_data_command(uint32_t message_id, const uint8_t *data,
                              size_t length, void *context)
{
    (void)context;
    if (message_id != C6_ZB_COMMAND_MSG_ID || data == NULL ||
        length != C6_ZB_COMMAND_SIZE || data[0] != C6_ZB_COMMAND_VERSION ||
        s_command_queue == NULL) {
        return;
    }
    const c6_zb_command_t command = {
        .command = data[1],
        .duration_seconds = data[2],
    };
    if (xQueueSend(s_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Command dropped: command=%u duration_s=%u queue_full=yes",
                 (unsigned)command.command,
                 (unsigned)command.duration_seconds);
    }
}

static void peer_data_task(void *context)
{
    (void)context;
    c6_zb_status_message_t message = {0};
    for (;;) {
        expire_commissioning_request();
        if (xQueueReceive(s_status_queue, &message,
                          pdMS_TO_TICKS(1000)) != pdTRUE) {
            publish_status();
            continue;
        }
        if (message.payload[0] == C6_ZB_STATUS_VERSION) {
            const esp_err_t result = eh_cp_feat_peer_data_send(
                C6_ZB_STATUS_MSG_ID, message.payload, sizeof(message.payload));
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Peer Data status send failed: %s",
                         esp_err_to_name(result));
            }
        }
    }
}

static bool zigbee_signal_handler(const ezb_app_signal_t *signal)
{
    if (signal == NULL) {
        return false;
    }
    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    const void *signal_params = ezb_app_signal_get_params(signal);
    const bool bdb_status_signal =
        type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ||
        type == EZB_BDB_SIGNAL_DEVICE_REBOOT ||
        type == EZB_BDB_SIGNAL_FORMATION;
    const int signal_status = bdb_status_signal && signal_params != NULL
        ? (int)*(const ezb_bdb_comm_status_t *)signal_params : -1;
    ESP_LOGI(TAG, "Zigbee signal: type=0x%04x bdb_status=%d",
             (unsigned)type, signal_status);
    portENTER_CRITICAL(&s_state_lock);
    s_last_signal_type = (uint16_t)type;
    portEXIT_CRITICAL(&s_state_lock);
    if (type == EZB_ZDO_SIGNAL_SKIP_STARTUP) {
        const ezb_err_t result = ezb_bdb_start_top_level_commissioning(
            EZB_BDB_MODE_INITIALIZATION);
        portENTER_CRITICAL(&s_state_lock);
        s_last_bdb_result = (int32_t)result;
        s_last_bdb_stage = C6_ZB_BDB_STAGE_INITIALIZATION;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGI(TAG, "BDB initialization request returned raw=%d",
                 (int)result);
        if (result != EZB_ERR_NONE) {
            portENTER_CRITICAL(&s_state_lock);
            s_last_error = esp_zigbee_err_to_esp(result);
            s_commissioning_started_us = 0;
            portEXIT_CRITICAL(&s_state_lock);
        }
        publish_status();
        return result == EZB_ERR_NONE;
    }
    if (type == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const ezb_zdo_signal_device_annce_params_t *params =
            (const ezb_zdo_signal_device_annce_params_t *)ezb_app_signal_get_params(signal);
        if (params == NULL) {
            return false;
        }
        portENTER_CRITICAL(&s_state_lock);
        bool known = false;
        for (size_t index = 0; index < C6_ZB_MAX_TRACKED_DEVICES; ++index) {
            if (s_device_ids[index] == params->device_addr.u64) {
                known = true;
                break;
            }
        }
        if (!known) {
            for (size_t index = 0; index < C6_ZB_MAX_TRACKED_DEVICES; ++index) {
                if (s_device_ids[index] == 0) {
                    s_device_ids[index] = params->device_addr.u64;
                    ++s_device_count;
                    break;
                }
            }
        }
        portEXIT_CRITICAL(&s_state_lock);
        publish_status();
        return true;
    }

    if (type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ||
        type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
        const ezb_bdb_comm_status_t *status =
            (const ezb_bdb_comm_status_t *)signal_params;
        if (status != NULL) {
            portENTER_CRITICAL(&s_state_lock);
            s_last_bdb_status = *status;
            portEXIT_CRITICAL(&s_state_lock);
        }
        if (status != NULL && *status == EZB_BDB_STATUS_SUCCESS) {
            bool search_pending = false;
            bool join_after_formation = false;
            portENTER_CRITICAL(&s_state_lock);
            s_bdb_ready = true;
            search_pending = s_search_pending;
            join_after_formation = s_join_after_formation;
            portEXIT_CRITICAL(&s_state_lock);
            if (search_pending && ezb_bdb_is_factory_new()) {
                const ezb_err_t result = ezb_bdb_start_top_level_commissioning(
                    EZB_BDB_MODE_NETWORK_FORMATION);
                portENTER_CRITICAL(&s_state_lock);
                s_last_bdb_result = (int32_t)result;
                s_last_bdb_stage = C6_ZB_BDB_STAGE_FORMATION_START;
                portEXIT_CRITICAL(&s_state_lock);
                ESP_LOGI(TAG, "BDB formation request after first-start returned raw=%d",
                         (int)result);
                if (result != EZB_ERR_NONE) {
                    portENTER_CRITICAL(&s_state_lock);
                    s_search_pending = false;
                    s_last_error = esp_zigbee_err_to_esp(result);
                    s_commissioning_started_us = 0;
                    portEXIT_CRITICAL(&s_state_lock);
                }
            } else if (search_pending && join_after_formation) {
                const ezb_err_t join_result = ezb_bdb_open_network(s_requested_join_seconds);
                const esp_err_t join_status = esp_zigbee_err_to_esp(join_result);
                portENTER_CRITICAL(&s_state_lock);
                s_network_formed = join_status == ESP_OK;
                s_search_pending = false;
                s_join_seconds = join_status == ESP_OK ? s_requested_join_seconds : 0;
                s_last_error = join_status;
                s_commissioning_started_us = 0;
                portEXIT_CRITICAL(&s_state_lock);
            } else if (!search_pending && !ezb_bdb_is_factory_new()) {
                portENTER_CRITICAL(&s_state_lock);
                s_network_formed = ezb_bdb_dev_joined();
                portEXIT_CRITICAL(&s_state_lock);
            } else if (search_pending) {
                portENTER_CRITICAL(&s_state_lock);
                s_search_pending = false;
                s_last_error = ESP_ERR_INVALID_STATE;
                s_commissioning_started_us = 0;
                portEXIT_CRITICAL(&s_state_lock);
            }
            if (search_pending) {
                portENTER_CRITICAL(&s_state_lock);
                if (!s_search_pending && s_last_error == ESP_OK) {
                    s_join_after_formation = false;
                }
                portEXIT_CRITICAL(&s_state_lock);
            }
            publish_status();
        } else if (status != NULL) {
            portENTER_CRITICAL(&s_state_lock);
            s_bdb_ready = false;
            s_search_pending = false;
            s_last_error = ESP_FAIL;
            s_last_bdb_status = *status;
            s_commissioning_started_us = 0;
            portEXIT_CRITICAL(&s_state_lock);
            publish_status();
        }
        return true;
    }

    if (type == EZB_BDB_SIGNAL_FORMATION) {
        const ezb_bdb_comm_status_t *status =
            (const ezb_bdb_comm_status_t *)signal_params;
        bool formed = status != NULL && *status == EZB_BDB_STATUS_SUCCESS;
        if (status != NULL) {
            portENTER_CRITICAL(&s_state_lock);
            s_last_bdb_status = *status;
            s_last_bdb_stage = C6_ZB_BDB_STAGE_FORMATION_SIGNAL;
            portEXIT_CRITICAL(&s_state_lock);
        }
        bool join_after_formation = false;
        uint8_t join_seconds = 0;
        portENTER_CRITICAL(&s_state_lock);
        join_after_formation = s_join_after_formation;
        join_seconds = s_requested_join_seconds;
        portEXIT_CRITICAL(&s_state_lock);
        esp_err_t result = formed ? ESP_OK : ESP_FAIL;
        ESP_LOGI(TAG, "BDB formation terminal signal: status=%d success=%s join_after=%s",
                 status == NULL ? -1 : (int)*status,
                 formed ? "yes" : "no",
                 join_after_formation ? "yes" : "no");
        if (formed) {
            const ezb_err_t close_result = ezb_bdb_close_network();
            portENTER_CRITICAL(&s_state_lock);
            s_last_bdb_result = (int32_t)close_result;
            s_last_bdb_stage = C6_ZB_BDB_STAGE_CLOSE_NETWORK;
            portEXIT_CRITICAL(&s_state_lock);
            result = esp_zigbee_err_to_esp(close_result);
            ESP_LOGI(TAG, "BDB close after formation returned raw=%d mapped=%s",
                     (int)close_result, esp_err_to_name(result));
            if (close_result == EZB_ERR_NONE && join_after_formation) {
                const ezb_err_t join_result = ezb_bdb_open_network(join_seconds);
                portENTER_CRITICAL(&s_state_lock);
                s_last_bdb_result = (int32_t)join_result;
                s_last_bdb_stage = C6_ZB_BDB_STAGE_OPEN_JOIN_WINDOW;
                portEXIT_CRITICAL(&s_state_lock);
                result = esp_zigbee_err_to_esp(join_result);
                ESP_LOGI(TAG, "BDB join-window after formation returned raw=%d mapped=%s seconds=%u",
                         (int)join_result, esp_err_to_name(result),
                         (unsigned)join_seconds);
            }
            formed = result == ESP_OK;
        }
        portENTER_CRITICAL(&s_state_lock);
        s_network_formed = formed;
        s_search_pending = false;
        s_formation_timed_out = false;
        s_join_after_formation = false;
        s_join_seconds = formed && join_after_formation ? join_seconds : 0;
        s_last_error = result;
        s_commissioning_started_us = 0;
        portEXIT_CRITICAL(&s_state_lock);
        publish_status();
        return true;
    }

    if (type == EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS) {
        const uint8_t *duration =
            (const uint8_t *)ezb_app_signal_get_params(signal);
        if (duration != NULL) {
            portENTER_CRITICAL(&s_state_lock);
            s_join_seconds = *duration;
            s_last_bdb_status = EZB_BDB_STATUS_SUCCESS;
            portEXIT_CRITICAL(&s_state_lock);
            publish_status();
        }
        return true;
    }
    return false;
}

static void zigbee_command_task(void *context)
{
    (void)context;
    c6_zb_command_t command = {0};
    for (;;) {
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (command.command == C6_ZB_COMMAND_STATUS) {
            publish_status();
            continue;
        }
        const bool legacy_search = command.command == C6_ZB_COMMAND_SEARCH;
        const bool create_network = command.command == C6_ZB_COMMAND_CREATE_NETWORK;
        const bool open_join_window = command.command == C6_ZB_COMMAND_OPEN_JOIN_WINDOW;
        ESP_LOGI(TAG, "Command dequeued: command=%u duration_s=%u",
             (unsigned)command.command,
             (unsigned)command.duration_seconds);
        if ((!legacy_search && !create_network && !open_join_window) ||
            (legacy_search && (command.duration_seconds == 0 || command.duration_seconds > 180)) ||
            (open_join_window && (command.duration_seconds == 0 || command.duration_seconds > 180)) ||
            (create_network && command.duration_seconds != 0)) {
            continue;
        }
        esp_err_t result = create_network ? persist_network_audit() : persist_search_audit();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Command audit failed: command=%u error=%s",
                     (unsigned)command.command, esp_err_to_name(result));
            portENTER_CRITICAL(&s_state_lock);
            s_last_error = result;
            portEXIT_CRITICAL(&s_state_lock);
            publish_status();
            continue;
        }
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
            ESP_LOGE(TAG, "Command could not acquire Zigbee lock: command=%u timeout_ms=1000",
                     (unsigned)command.command);
            portENTER_CRITICAL(&s_state_lock);
            s_last_error = ESP_ERR_TIMEOUT;
            portEXIT_CRITICAL(&s_state_lock);
            publish_status();
            continue;
        }
        portENTER_CRITICAL(&s_state_lock);
        const bool bdb_ready = s_bdb_ready;
        const bool network_formed = s_network_formed;
        const bool formation_timed_out = s_formation_timed_out;
        portEXIT_CRITICAL(&s_state_lock);
        const bool factory_new = ezb_bdb_is_factory_new();
        ESP_LOGI(TAG, "Commissioning preflight: bdb_ready=%s factory_new=%s network_formed=%s timed_out=%s",
             bdb_ready ? "yes" : "no", factory_new ? "yes" : "no",
             network_formed ? "yes" : "no",
             formation_timed_out ? "yes" : "no");

        if (!bdb_ready || (create_network &&
                   (formation_timed_out || !factory_new)) ||
            (open_join_window && !network_formed)) {
            ESP_LOGE(TAG, "Commissioning rejected by preflight: command=%u",
                 (unsigned)command.command);
            portENTER_CRITICAL(&s_state_lock);
            s_last_error = ESP_ERR_INVALID_STATE;
            s_search_pending = false;
            s_commissioning_started_us = 0;
            portEXIT_CRITICAL(&s_state_lock);
            esp_zigbee_lock_release();
            publish_status();
            continue;
        }

        portENTER_CRITICAL(&s_state_lock);
        s_search_pending = legacy_search || create_network;
        s_formation_timed_out = false;
        s_join_after_formation = legacy_search;
        s_requested_join_seconds = command.duration_seconds;
        s_last_error = ESP_OK;
        s_commissioning_started_us = s_search_pending ? esp_timer_get_time() : 0;
        s_last_bdb_status = EZB_BDB_STATUS_IN_PROGRESS;
        portEXIT_CRITICAL(&s_state_lock);

        ezb_err_t zigbee_result;
        if (create_network || (legacy_search && ezb_bdb_is_factory_new())) {
            zigbee_result = ezb_bdb_start_top_level_commissioning(
                EZB_BDB_MODE_NETWORK_FORMATION);
        } else if (legacy_search || open_join_window) {
            zigbee_result = ezb_bdb_open_network(command.duration_seconds);
            if (zigbee_result == EZB_ERR_NONE) {
                portENTER_CRITICAL(&s_state_lock);
                s_network_formed = true;
                s_search_pending = false;
                s_join_after_formation = false;
                s_join_seconds = command.duration_seconds;
                portEXIT_CRITICAL(&s_state_lock);
            }
        } else {
            zigbee_result = EZB_ERR_FAIL;
        }
        portENTER_CRITICAL(&s_state_lock);
        s_last_bdb_result = (int32_t)zigbee_result;
        s_last_bdb_stage = (create_network ||
                            (legacy_search && ezb_bdb_is_factory_new()))
            ? C6_ZB_BDB_STAGE_FORMATION_START
            : C6_ZB_BDB_STAGE_OPEN_JOIN_WINDOW;
        portEXIT_CRITICAL(&s_state_lock);
        esp_zigbee_lock_release();
        result = esp_zigbee_err_to_esp(zigbee_result);
        ESP_LOGI(TAG, "Command start returned: command=%u raw=%d mapped=%s",
             (unsigned)command.command, (int)zigbee_result,
             esp_err_to_name(result));
        if (result != ESP_OK) {
            portENTER_CRITICAL(&s_state_lock);
            s_search_pending = false;
            s_last_error = result;
            s_commissioning_started_us = 0;
            portEXIT_CRITICAL(&s_state_lock);
        }
        publish_status();
    }
}

static void zigbee_task(void *context)
{
    (void)context;
    esp_zigbee_config_t config = {0};
    config.device_config.device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR;
    config.device_config.install_code_policy = false;
    config.device_config.zczr_config.max_children = 16;
    config.platform_config.storage_partition_name = "nvs";
    config.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE;

    esp_err_t result = esp_zigbee_init(&config);
    if (result == ESP_OK) {
        ezb_aps_secur_enable_distributed_security(false);
    }
        const ezb_err_t primary_channel = result == ESP_OK
        ? ezb_bdb_set_primary_channel_set(0x07FFF800U) : EZB_ERR_FAIL;
    const ezb_err_t secondary_channel = result == ESP_OK
        ? ezb_bdb_set_secondary_channel_set(0x07FFF800U) : EZB_ERR_FAIL;
    const esp_err_t signal_status = result == ESP_OK
        ? ezb_app_signal_add_handler(zigbee_signal_handler) : ESP_FAIL;
    if (result != ESP_OK || primary_channel != EZB_ERR_NONE ||
        secondary_channel != EZB_ERR_NONE ||
        signal_status != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_last_error = result == ESP_OK ? ESP_FAIL : result;
        portEXIT_CRITICAL(&s_state_lock);
        publish_status();
        ESP_LOGE(TAG, "Native Zigbee coordinator init failed");
        vTaskDelete(NULL);
    }

    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_zha_custom_gateway_config_t gateway_config = EZB_ZHA_CUSTOM_GATEWAY_CONFIG();
    ezb_af_ep_desc_t endpoint = ezb_zha_create_custom_gateway(1, &gateway_config);
    if (device == NULL || endpoint == NULL ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||
        ezb_af_device_desc_register(device) != EZB_ERR_NONE) {
        portENTER_CRITICAL(&s_state_lock);
        s_last_error = ESP_FAIL;
        portEXIT_CRITICAL(&s_state_lock);
        publish_status();
        ESP_LOGE(TAG, "Native Zigbee coordinator endpoint setup failed");
        vTaskDelete(NULL);
    }

    result = esp_zigbee_start(false);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_last_error = result;
        portEXIT_CRITICAL(&s_state_lock);
        publish_status();
        ESP_LOGE(TAG, "Native Zigbee coordinator start failed: %s", esp_err_to_name(result));
        vTaskDelete(NULL);
    }
    portENTER_CRITICAL(&s_state_lock);
    s_stack_ready = true;
    portEXIT_CRITICAL(&s_state_lock);
    publish_status();
    ESP_LOGI(TAG, "Native Zigbee coordinator ready; network remains closed");
    (void)esp_zigbee_launch_mainloop();
    vTaskDelete(NULL);
}

esp_err_t c6_zigbee_sdio_start(void)
{
    esp_err_t result = esp_coex_wifi_i154_enable();
    if (result != ESP_OK) {
        return result;
    }
    s_status_queue = xQueueCreate(8, sizeof(c6_zb_status_message_t));
    s_command_queue = xQueueCreate(2, sizeof(c6_zb_command_t));
    if (s_status_queue == NULL || s_command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    result = eh_cp_feat_peer_data_init();
    if (result != ESP_OK) {
        return result;
    }
    result = eh_cp_feat_peer_data_register_callback(
        C6_ZB_COMMAND_MSG_ID, peer_data_command, NULL);
    if (result != ESP_OK) {
        return result;
    }
    if (xTaskCreate(peer_data_task, "c6_zb_peer", 4096, NULL, 5, &s_peer_data_task) != pdPASS ||
        xTaskCreate(zigbee_command_task, "c6_zb_cmd", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(zigbee_task, "c6_zb_stack", 8192, NULL, 5, &s_zigbee_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}