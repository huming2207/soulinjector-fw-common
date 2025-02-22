#include <multi_heap.h>
#include <mqtt_client.hpp>
#include <esp_efuse.h>
#include <esp_http_client.h>
#include <esp_mac.h>
#include <esp_flash.h>
#include "rpc_report_packet.hpp"
#include "rpc_cmd_packet.hpp"
#include "mq_defs.hpp"

esp_err_t mqtt_client::init(esp_mqtt_client_config_t *_mqtt_cfg)
{
    memcpy(&mqtt_cfg, _mqtt_cfg, sizeof(esp_mqtt_client_config_t));
    mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_handle != nullptr) {
        return ESP_FAIL;
    }

    mqtt_state = xEventGroupCreate();
    if (mqtt_state == nullptr) {
        ESP_LOGE(TAG, "Failed to create state event group");
        return ESP_ERR_NO_MEM;
    }

    auto ret = esp_efuse_mac_get_default(host_sn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Can't read MAC address: 0x%x", ret);
        return ret;
    }

    cmd_queue = xQueueCreateWithCaps(32, sizeof(mq_cmd_pkt), MALLOC_CAP_SPIRAM);
    if (cmd_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create cmd queue");
        return ESP_ERR_NO_MEM;
    }

    return esp_mqtt_client_register_event(mqtt_handle, MQTT_EVENT_ANY, mq_event_handler, this);;
}

esp_err_t mqtt_client::connect()
{
    if (mqtt_state != nullptr) {
        xEventGroupClearBits(mqtt_state, MQ_STATE_FORCE_DISCONNECT);
    }

    return esp_mqtt_client_start(mqtt_handle);
}

esp_err_t mqtt_client::disconnect()
{
    if (mqtt_state != nullptr) {
        xEventGroupSetBits(mqtt_state, MQ_STATE_FORCE_DISCONNECT);
    }

    return esp_mqtt_client_disconnect(mqtt_handle);
}

esp_err_t mqtt_client::report_stuff(rpc::report::base_event *event, const char *event_subtopic)
{
    if (event_subtopic == nullptr) {
        ESP_LOGE(TAG, "record: invalid arg %p %p", event, event_subtopic);
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(event_subtopic) > 16) {
        ESP_LOGE(TAG, "Event subtopic too long: %u", strlen(event_subtopic));
        return ESP_ERR_NO_MEM;
    }

    char topic_full[sizeof(mq::TOPIC_REPORT_BASE) + sizeof(host_sn) + 32] = {};
    snprintf(topic_full, sizeof(topic_full), "%s/" MACSTR "/%s", mq::TOPIC_REPORT_BASE, MAC2STR(host_sn), event_subtopic);
    topic_full[sizeof(topic_full) - 1] = '\0';

    uint8_t msgpack_buf_stack[256] = {};
    uint8_t *msgpack_buf = nullptr;
    size_t expected_size = event->get_serialized_size();
    bool heap_allocated = expected_size > sizeof(msgpack_buf_stack);
    if (heap_allocated) {
        ESP_LOGW(TAG, "Alloc'ing msgpack buffer on heap, size=%u topic=%s", expected_size, event_subtopic);
        msgpack_buf = (uint8_t *) heap_caps_calloc(1, expected_size, MALLOC_CAP_SPIRAM);
        if (msgpack_buf == nullptr) {
            ESP_LOGE(TAG, "Heap alloc failed, size=%u topic=%s", expected_size, event_subtopic);
            return ESP_ERR_NO_MEM;
        }
    } else {
        msgpack_buf = msgpack_buf_stack;
    }

    size_t serialised_len = event->serialize(msgpack_buf, sizeof(msgpack_buf));
    int ret = esp_mqtt_client_enqueue(mqtt_handle, topic_full, (const char *)msgpack_buf, (int)serialised_len, 1, 1, true);

    if (heap_allocated) {
        free(msgpack_buf);
    }

    if (ret == -1) {
        ESP_LOGE(TAG, "record: failed to enqueue, dunno why");
        return ESP_FAIL;
    } else if (ret == -2) {
        ESP_LOGE(TAG, "record: MQTT queue full!");
        return ESP_ERR_NO_MEM; // -2 means queue is full
    }

    return ESP_OK;
}

esp_err_t mqtt_client::report_init(rpc::report::init_event *init_evt)
{
    return report_stuff(init_evt, mq::TOPIC_REPORT_INIT);
}

esp_err_t mqtt_client::report_host_state(rpc::report::state_event *state_evt)
{
    return report_stuff(state_evt, mq::TOPIC_REPORT_HOST_STATE);
}

esp_err_t mqtt_client::report_host_state(const char *msg, esp_err_t ret)
{
    rpc::report::state_event event = {};
    event.err_code = ret;
    event.msg_str = msg;

    return report_host_state(&event);
}

esp_err_t mqtt_client::report_erase(rpc::report::erase_event *erase_evt)
{
    return report_stuff(erase_evt, mq::TOPIC_REPORT_ERASE);
}

esp_err_t mqtt_client::report_program(rpc::report::prog_event *prog_evt)
{
    return report_stuff(prog_evt, mq::TOPIC_REPORT_PROG);
}

esp_err_t mqtt_client::report_self_test(rpc::report::self_test_event *test_evt, uint8_t *result_payload, size_t payload_len)
{
    return report_stuff(test_evt, mq::TOPIC_REPORT_SELF_TEST);
}

esp_err_t mqtt_client::report_repair(rpc::report::repair_event *repair_evt)
{
    return report_stuff(repair_evt, mq::TOPIC_REPORT_REPAIR);
}

esp_err_t mqtt_client::report_dispose(rpc::report::repair_event *repair_evt)
{
    return report_stuff(repair_evt, mq::TOPIC_REPORT_DISPOSE);
}

void mqtt_client::mq_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    auto *ctx = (mqtt_client *)handler_args;
    auto *mqtt_evt = (esp_mqtt_event_handle_t)event_data;

    if (ctx == nullptr || mqtt_evt == nullptr) {
        return;
    }

    switch (mqtt_evt->event_id) {
        case MQTT_EVENT_ERROR: {
            break;
        }
        case MQTT_EVENT_CONNECTED: {
            if (ctx->subscribe_on_connect() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to subscribe, disconnect now!");
                xEventGroupClearBits(ctx->mqtt_state, MQ_STATE_REGISTERED);
                esp_mqtt_client_disconnect(mqtt_evt->client);
            } else {
                ESP_LOGI(TAG, "Subscribe OK");
                xEventGroupSetBits(ctx->mqtt_state, MQ_STATE_REGISTERED);
            }

            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            ESP_LOGW(TAG, "MQTT Disconnected!");
            if ((xEventGroupGetBits(ctx->mqtt_state) & MQ_STATE_FORCE_DISCONNECT) == 0) {
                ESP_LOGI(TAG, "Eagerly reconnecting...");
                auto ret = esp_mqtt_client_reconnect(ctx->mqtt_handle);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Reconnect fail, 0x%x %s", ret, esp_err_to_name(ret));
                }
            }
            break;
        }

        case MQTT_EVENT_SUBSCRIBED: {
            break;
        }
        case MQTT_EVENT_UNSUBSCRIBED: {
            break;
        }
        case MQTT_EVENT_PUBLISHED: {
            break;
        }
        case MQTT_EVENT_DATA: {
            ctx->decode_cmd_msg(mqtt_evt->topic, mqtt_evt->topic_len, (uint8_t *) (mqtt_evt->data), mqtt_evt->data_len);
            break;
        }
        case MQTT_EVENT_BEFORE_CONNECT: {
            xEventGroupClearBits(ctx->mqtt_state, MQ_STATE_REGISTERED);
            break;
        }
        case MQTT_EVENT_DELETED: {
            break;
        }
        default: {
            break;
        }
    }
}

esp_err_t mqtt_client::subscribe_on_connect()
{
    char topic_str[sizeof(mq::TOPIC_CMD_BASE) + (sizeof(host_sn) * 2) + 16] = {};
    esp_mqtt_topic_t topics[4] = {};

    snprintf(topic_str, sizeof(topic_str), "%s/" MACSTR "/%s", mq::TOPIC_CMD_BASE, MAC2STR(host_sn), mq::TOPIC_CMD_READ_MEM);
    topic_str[sizeof(topic_str) - 1] = '\0';
    topics[0].filter = strdup((const char *)topic_str);
    topics[0].qos = 2;
    memset(topic_str, 0, sizeof(topic_str));

    snprintf(topic_str, sizeof(topic_str), "%s/" MACSTR "/%s", mq::TOPIC_CMD_BASE, MAC2STR(host_sn), mq::TOPIC_CMD_SET_STATE);
    topic_str[sizeof(topic_str) - 1] = '\0';
    topics[1].filter = strdup((const char *)topic_str);
    topics[1].qos = 2;
    memset(topic_str, 0, sizeof(topic_str));

    snprintf(topic_str, sizeof(topic_str), "%s/" MACSTR "/%s", mq::TOPIC_CMD_BASE, MAC2STR(host_sn), mq::TOPIC_CMD_METADATA_FIRMWARE);
    topic_str[sizeof(topic_str) - 1] = '\0';
    topics[2].filter = strdup((const char *)topic_str);
    topics[2].qos = 2;
    memset(topic_str, 0, sizeof(topic_str));

    snprintf(topic_str, sizeof(topic_str), "%s/" MACSTR "/%s", mq::TOPIC_CMD_BASE, MAC2STR(host_sn), mq::TOPIC_CMD_METADATA_FLASH_ALGO);
    topic_str[sizeof(topic_str) - 1] = '\0';
    topics[3].filter = strdup((const char *)topic_str);
    topics[3].qos = 2;
    memset(topic_str, 0, sizeof(topic_str));


    auto ret = esp_mqtt_client_subscribe_multiple(mqtt_handle, topics, 4);
    ESP_LOGI(TAG, "Subscribing to %s, ret=%d", topic_str, ret);

    for (size_t idx = 0; idx < 4; idx += 1) {
        if (topics[idx].filter != nullptr) {
            free((void *) topics[idx].filter);
            topics[idx].filter = nullptr;
        }
    }

    if (ret < 0) {
        if (ret == -1) {
            ESP_LOGE(TAG, "Failed to subscribe, unknown error");
            return ESP_FAIL;
        } else if (ret == -2) {
            ESP_LOGE(TAG, "Failed to subscribe, msgbox full");
            return ESP_ERR_NO_MEM;
        } else {
            ESP_LOGE(TAG, "Unknown state: %d", ret);
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        return ESP_OK;
    }
}

esp_err_t mqtt_client::decode_cmd_msg(const char *topic, size_t topic_len, uint8_t *buf, size_t buf_len)
{
    if (topic == nullptr || topic_len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if the topic is the command message
    if (strnstr(topic, mq::TOPIC_CMD_BASE, std::min(sizeof(mq::TOPIC_CMD_BASE), topic_len)) == nullptr) {
        ESP_LOGW(TAG, "Invalid cmd message: %s", topic);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Check topic type & decode accordingly
    mq_cmd_pkt cmd = {};
    esp_err_t ret = ESP_OK;
    if (strnstr(topic, mq::TOPIC_CMD_METADATA_FIRMWARE, std::min(sizeof(mq::TOPIC_CMD_METADATA_FIRMWARE), topic_len)) != nullptr) {
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_META_FW, buf, buf_len);
    } else if (strnstr(topic, mq::TOPIC_CMD_METADATA_FLASH_ALGO, std::min(sizeof(mq::TOPIC_CMD_METADATA_FLASH_ALGO), topic_len)) != nullptr) {
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_META_ALGO, buf, buf_len);
    } else if (strnstr(topic, mq::TOPIC_CMD_BIN_FIRMWARE, std::min(sizeof(mq::TOPIC_CMD_BIN_FIRMWARE), topic_len)) != nullptr) {
        xEventGroupSetBits(mqtt_state, MQ_STATE_BIN_REQ_READY);
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_BIN_FW, buf, buf_len);
    } else if (strnstr(topic, mq::TOPIC_CMD_BIN_FLASH_ALGO, std::min(sizeof(mq::TOPIC_CMD_BIN_FLASH_ALGO), topic_len)) != nullptr) {
        xEventGroupSetBits(mqtt_state, MQ_STATE_BIN_REQ_READY);
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_BIN_ALGO, buf, buf_len);
    } else if (strnstr(topic, mq::TOPIC_CMD_READ_MEM, std::min(sizeof(mq::TOPIC_CMD_READ_MEM), topic_len)) != nullptr) {
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_READ_MEM, buf, buf_len);
    } else if (strnstr(topic, mq::TOPIC_CMD_SET_STATE, std::min(sizeof(mq::TOPIC_CMD_SET_STATE), topic_len)) != nullptr) {
        ret = make_mq_cmd_packet(&cmd, MQ_CMD_SET_STATE, buf, buf_len);
    } else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    if (ret != ESP_OK) {
        return ret;
    }

    if (xQueueSend(cmd_queue, &cmd, pdMS_TO_TICKS(CONFIG_SI_MQ_RECV_TIMEOUT)) == pdFALSE) {
        ESP_LOGE(TAG, "CMD queue full!");
        return ESP_ERR_TIMEOUT;
    }
    return 0;
}

esp_err_t mqtt_client::recv_cmd_packet(mqtt_client::mq_cmd_pkt *cmd_pkt, uint32_t timeout_ticks)
{
    if (cmd_pkt == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return xQueueReceive(cmd_queue, cmd_pkt, timeout_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t mqtt_client::request_blob(const char *type, uint32_t offset, size_t expect_blk_len, uint32_t timeout_ticks)
{
    if ((xEventGroupWaitBits(mqtt_state, MQ_STATE_BIN_REQ_READY, pdTRUE, pdFALSE, timeout_ticks) & MQ_STATE_BIN_REQ_READY) == 0) {
        ESP_LOGE(TAG, "Request blob timeout, try again later");
        return ESP_ERR_TIMEOUT;
    }

    char topic_str[255] = {};
    snprintf(topic_str, sizeof(topic_str), "%s/" MACSTR "/%s/%lu/%u", mq::TOPIC_CMD_BASE, MAC2STR(host_sn), type, offset, expect_blk_len);

    return esp_mqtt_client_subscribe_single(mqtt_handle, topic_str, 1);
}
