#ifndef WIFI_H_
#define WIFI_H_
//-------------------------------------------------------------

// Параметры точки доступа (режим конфигурации)
#define WIFI_AP_SSID       "Domofon-Setup"
#define WIFI_AP_PASS       "12345678"
#define WIFI_AP_MAX_CONN   2
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_IP         "192.168.4.1"

// Страница конфигурации
#define WIFI_CONFIG_URL    "/setup" // http://192.168.4.1/setup

// Параметры подключения к STA
#define WIFI_STA_MAX_RETRY 5

// Частота моргания в режиме конфигурации (мс)
#define WIFI_BLINK_PERIOD  200
//-------------------------------------------------------------
void initialize_wifi(void);
//-------------------------------------------------------------
#endif /* WIFI_H_ */
