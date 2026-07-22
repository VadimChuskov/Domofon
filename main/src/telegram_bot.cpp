#include "telegram_bot.h"
//-------------------------------------------------------------
#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
//-------------------------------------------------------------

static const char *TAG = "TELEGRAM";

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
