#include <stdio.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "gpio_int.h"
#include "led.h"
#include "wifi.h"
#include "telegram_bot.h"
#include "gpio_out.h"

//------------------------------------------
#define DOOR_OPEN_GPIO GPIO_NUM_33
//------------------------------------------

static const char *TAG = "Domofon";
static const int INTERRUPT_DELAY_MS = 10000;
static const int DOOR_OPEN_DURATION_MS = 1000;

//---------------------------------------
void gpio_interrupt_callback(int level);
//--------------------------------------

static void telegram_poll_task(void *arg)
{
    wifi_wait_connected();
    ESP_LOGI(TAG, "WiFi connected, starting Telegram polling");

    while (1) {
        wifi_wait_connected(); // на случай отвала вайфая, чтобы не пытаться слать запросы без сети

        if (telegram_check_updates()) {
            ESP_LOGI(TAG, "Opening door...");
            gpio_output_on(DOOR_OPEN_GPIO);
            led_blink(50);
            vTaskDelay(pdMS_TO_TICKS(DOOR_OPEN_DURATION_MS));
            gpio_output_off(DOOR_OPEN_GPIO);
            led_off();
        }
    }
}

extern "C" void app_main(void)
{
    initialize_led();
    initialize_wifi();
    initialize_gpio_interrupt(gpio_interrupt_callback);
    initialize_gpio_output(DOOR_OPEN_GPIO);

    xTaskCreate(telegram_poll_task, "tg_poll", 8192, NULL, 5, NULL);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void gpio_interrupt_callback(int level)
{
    ESP_LOGW(TAG, "GPIO level = %d", level);

    led_blink(1000);
    telegram_send_message("Call from ESP32");
    vTaskDelay(pdMS_TO_TICKS(INTERRUPT_DELAY_MS));
    led_off();
    gpio_interrupt_on();
}