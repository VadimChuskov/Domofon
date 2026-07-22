#include "telegram_bot.h"
//-------------------------------------------------------------
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
//-------------------------------------------------------------

static const char *TAG = "TELEGRAM";
static long long update_offset = 0;

//-------------------------------------------------------------
// Буфер для ответа getUpdates
//-------------------------------------------------------------
#define RESPONSE_BUF_SIZE 2048
static char response_buf[RESPONSE_BUF_SIZE];
static int response_len;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (response_len + evt->data_len < RESPONSE_BUF_SIZE - 1) {
            memcpy(response_buf + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buf[response_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

//-------------------------------------------------------------
// Отправка сообщения в Telegram
//-------------------------------------------------------------
esp_err_t telegram_send_message(const char *text)
{
    char url[128];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN "/sendMessage");

    // Формируем тело POST-запроса: chat_id=...&text=...
    char post_data[512];
    int len = snprintf(post_data, sizeof(post_data),
                       "chat_id=%s&text=%s", TELEGRAM_CHAT_ID, text);
    if (len < 0 || len >= (int)sizeof(post_data)) {
        ESP_LOGE(TAG, "Message too long");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Message sent, HTTP status: %d", status);
        if (status != 200) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

//-------------------------------------------------------------
// Проверка новых сообщений через getUpdates
//-------------------------------------------------------------
bool telegram_check_updates(void)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN
             "/getUpdates?offset=%lld&limit=1&timeout=30",
             update_offset);

    response_len = 0;
    response_buf[0] = '\0';

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 35000; // 35 секунд, чтобы учесть таймаут long polling = 30 секунд
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = http_event_handler;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "getUpdates failed: %s", esp_err_to_name(err));
        return false;
    }

    // Простой разбор: ищем "update_id": и извлекаем число
    char *uid_ptr = strstr(response_buf, "\"update_id\":");
    if (uid_ptr == NULL) {
        return false;
    }

    uid_ptr += strlen("\"update_id\":");
    long long uid = strtoll(uid_ptr, NULL, 10);
    if (uid >= update_offset) {
        update_offset = uid + 1;
        ESP_LOGI(TAG, "New message received, update_id=%lld", uid);
        return true;
    }

    return false;
}
