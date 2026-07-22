#include <stdio.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "gpio_int.h"
#include "led.h"
#include "wifi.h"
#include "telegram_bot.h"
#include "gpio_out.h"

static const char *TAG = "Domofon";
static const int INTERRUPT_DELAY_MS = 10000;

//---------------------------------------
void gpio_interrupt_callback(int level);
//--------------------------------------

extern "C" void app_main(void)
{
    initialize_led();
    initialize_wifi();
    initialize_gpio_interrupt(gpio_interrupt_callback);
    initialize_gpio_output(GPIO_NUM_33);

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