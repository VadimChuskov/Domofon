#include "wifi.h"
//-------------------------------------------------------------
#include "led.h"
//-------------------------------------------------------------
#include <cstring>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
//-------------------------------------------------------------

static const char *TAG = "WIFI";

// NVS namespace и ключи
static const char *NVS_NAMESPACE   = "wifi_creds";
static const char *NVS_KEY_SSID    = "ssid";
static const char *NVS_KEY_PASS    = "pass";

// Группа событий для отслеживания подключения
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_count = 0;
static httpd_handle_t s_server = NULL;
static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap  = NULL;

//-------------------------------------------------------------
// Forward declarations
//-------------------------------------------------------------
static void start_ap_mode(void);
static void attempt_sta_connect(const char *ssid, const char *pass);
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void stop_webserver(void);

//-------------------------------------------------------------
// NVS: сохранить креды
//-------------------------------------------------------------
static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

//-------------------------------------------------------------
// NVS: прочитать креды (true = найдены)
//-------------------------------------------------------------
static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) { nvs_close(handle); return false; }

    err = nvs_get_str(handle, NVS_KEY_PASS, pass, &pass_len);
    if (err != ESP_OK) { nvs_close(handle); return false; }

    nvs_close(handle);
    return strlen(ssid) > 0;
}

//-------------------------------------------------------------
// HTML страница настройки
//-------------------------------------------------------------
static const char SETUP_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Domofon Wi-Fi Setup</title>"
    "<style>"
    "body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;"
    "min-height:100vh;margin:0;background:#f0f0f0}"
    ".card{background:#fff;padding:2em;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.15);"
    "width:300px}"
    "h2{margin-top:0;text-align:center}"
    "input{width:100%;padding:10px;margin:6px 0 14px;border:1px solid #ccc;border-radius:6px;"
    "box-sizing:border-box;font-size:1em}"
    "button{width:100%;padding:12px;background:#4CAF50;color:#fff;border:none;"
    "border-radius:6px;font-size:1.1em;cursor:pointer}"
    "button:hover{background:#45a049}"
    "</style></head><body>"
    "<div class='card'>"
    "<h2>Wi-Fi Setup</h2>"
    "<form method='POST' action='/setup'>"
    "<label>SSID:</label>"
    "<input name='ssid' maxlength='32' required>"
    "<label>Password:</label>"
    "<input name='pass' type='password' maxlength='64'>"
    "<button type='submit'>Save &amp; Connect</button>"
    "</form></div></body></html>";

//-------------------------------------------------------------
// URL-decode (in-place)
//-------------------------------------------------------------
static void url_decode(char *str)
{
    char *dst = str;
    while (*str) {
        if (*str == '%' && str[1] && str[2]) {
            char hex[3] = { str[1], str[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else if (*str == '+') {
            *dst++ = ' ';
            str++;
        } else {
            *dst++ = *str++;
        }
    }
    *dst = '\0';
}

//-------------------------------------------------------------
// Парсинг form-urlencoded параметра
//-------------------------------------------------------------
static bool parse_form_param(const char *body, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        // Проверяем что это начало параметра (начало строки или после &)
        if (p != body && *(p - 1) != '&') {
            p += key_len;
            continue;
        }
        if (p[key_len] != '=') {
            p += key_len;
            continue;
        }
        p += key_len + 1; // пропускаем "key="
        const char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= out_len) len = out_len - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        url_decode(out);
        return true;
    }
    return false;
}

//-------------------------------------------------------------
// HTTP GET /setup
//-------------------------------------------------------------
static esp_err_t setup_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, sizeof(SETUP_HTML) - 1);
    return ESP_OK;
}

//-------------------------------------------------------------
// Задача переподключения (запускается из POST-обработчика)
//-------------------------------------------------------------
struct reconnect_params_t {
    char ssid[33];
    char pass[65];
};

static void reconnect_task(void *arg)
{
    auto *params = (reconnect_params_t *)arg;

    vTaskDelay(pdMS_TO_TICKS(1000));

    stop_webserver();
    esp_wifi_stop();

    attempt_sta_connect(params->ssid, params->pass);

    delete params;
    vTaskDelete(NULL);
}

//-------------------------------------------------------------
// HTTP POST /setup
//-------------------------------------------------------------
static esp_err_t setup_post_handler(httpd_req_t *req)
{
    char body[200] = {};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {};
    char pass[65] = {};

    if (!parse_form_param(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    parse_form_param(body, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "Received credentials: SSID='%s'", ssid);

    esp_err_t err = save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write error");
        return ESP_FAIL;
    }

    const char *resp =
        "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body>"
        "<h2>Saved! Connecting...</h2>"
        "<p>The device will now try to connect to the specified network.</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, strlen(resp));

    // Переподключение в отдельной задаче, чтобы не блокировать httpd
    auto *params = new reconnect_params_t();
    strncpy(params->ssid, ssid, sizeof(params->ssid) - 1);
    strncpy(params->pass, pass, sizeof(params->pass) - 1);
    xTaskCreate(reconnect_task, "reconnect", 4096, params, 5, NULL);

    return ESP_OK;
}

//-------------------------------------------------------------
// Запуск HTTP сервера
//-------------------------------------------------------------
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t get_uri = {
        .uri      = WIFI_CONFIG_URL,
        .method   = HTTP_GET,
        .handler  = setup_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &get_uri);

    httpd_uri_t post_uri = {
        .uri      = WIFI_CONFIG_URL,
        .method   = HTTP_POST,
        .handler  = setup_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &post_uri);

    ESP_LOGI(TAG, "HTTP server started, config page at http://" WIFI_AP_IP "%s", WIFI_CONFIG_URL);
    return server;
}

//-------------------------------------------------------------
// Остановка HTTP сервера
//-------------------------------------------------------------
static void stop_webserver(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

//-------------------------------------------------------------
// Обработчик событий Wi-Fi и IP
//-------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_count < WIFI_STA_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Retrying connection (%d/%d)...", s_retry_count, WIFI_STA_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Failed to connect after %d attempts", WIFI_STA_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//-------------------------------------------------------------
// Попытка подключения к STA
//-------------------------------------------------------------
static void attempt_sta_connect(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);
    led_on();
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(pass) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Ждём результат подключения
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to '%s'", ssid);
        led_off();
    } else {
        ESP_LOGW(TAG, "Connection to '%s' failed, switching to AP mode", ssid);
        esp_wifi_stop();
        start_ap_mode();
    }
}

//-------------------------------------------------------------
// Запуск режима точки доступа (AP) + веб-сервер
//-------------------------------------------------------------
static void start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode: SSID='%s'", WIFI_AP_SSID);
    led_blink(WIFI_BLINK_PERIOD);

    wifi_config_t ap_config = {};
    strncpy((char *)ap_config.ap.ssid, WIFI_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    strncpy((char *)ap_config.ap.password, WIFI_AP_PASS, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_server = start_webserver();
}

//-------------------------------------------------------------
// Задача Wi-Fi (запускается из initialize_wifi)
//-------------------------------------------------------------
static void wifi_task(void *arg)
{
    char ssid[33] = {};
    char pass[65] = {};

    led_on();

    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Found saved credentials for '%s'", ssid);
        attempt_sta_connect(ssid, pass);
    } else {
        ESP_LOGI(TAG, "No saved credentials found");
        start_ap_mode();
    }

    vTaskDelete(NULL);
}

//-------------------------------------------------------------
// Инициализация Wi-Fi модуля (вызывается из main)
//-------------------------------------------------------------
void initialize_wifi(void)
{
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация сетевого стека
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Регистрация обработчиков событий
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_wifi_event_group = xEventGroupCreate();

    // Запуск в отдельной задаче, чтобы не блокировать main
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Wi-Fi module initialized");
}

void wifi_wait_connected(void)
{
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
}
